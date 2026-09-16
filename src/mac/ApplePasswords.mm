#include "ApplePasswords.hpp"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>
#import <objc/message.h>
#import <objc/runtime.h>

#include <dlfcn.h>

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QOperatingSystemVersion>
#include <QRandomGenerator>
#include <QSet>
#include <QTextStream>
#include <QThread>

Q_LOGGING_CATEGORY(lcPasswords, "pocb.passwords")

namespace mac {
namespace {

constexpr OSStatus kMissingEntitlement = -34018;  // errSecMissingEntitlement
constexpr const char *kSafariSharedPath = "/System/Library/PrivateFrameworks/SafariShared.framework/SafariShared";
constexpr const char *kASCorePath = "/System/Library/PrivateFrameworks/AuthenticationServicesCore.framework/AuthenticationServicesCore";
constexpr const char *kASCoreVersionedPath = "/System/Library/PrivateFrameworks/AuthenticationServicesCore.framework/Versions/A/AuthenticationServicesCore";

// Items pocb stores itself carry this security domain so they never collide
// with other apps' internet passwords for the same host.
NSString *const kOwnService = @"pocb.apple-passwords";
NSString *const kOwnLabelPrefix = @"pocb — ";

template <typename R, typename... Args>
R msgSend(id target, SEL sel, Args... args) {
    using Fn = R (*)(id, SEL, Args...);
    return reinterpret_cast<Fn>(objc_msgSend)(target, sel, args...);
}

bool responds(id obj, SEL sel) { return obj && sel && [obj respondsToSelector:sel]; }

void runOnMain(std::function<void()> fn) {
    if (!fn) return;
    if (QThread::currentThread() == QCoreApplication::instance()->thread()) { fn(); return; }
    QMetaObject::invokeMethod(QCoreApplication::instance(), [fn = std::move(fn)] { fn(); }, Qt::QueuedConnection);
}

void *loadSafariShared() {
    static void *handle = dlopen(kSafariSharedPath, RTLD_NOW | RTLD_GLOBAL);
    return handle;
}

void *loadASCore() {
    static void *handle = [] {
        void *h = dlopen(kASCorePath, RTLD_NOW | RTLD_GLOBAL);
        if (!h) h = dlopen(kASCoreVersionedPath, RTLD_NOW | RTLD_GLOBAL);
        return h;
    }();
    return handle;
}

// ---- keychain capability probes -------------------------------------------

// Safari/Passwords store logins in the Apple-private keychain access group
// com.apple.cfnetwork. Only Apple-signed processes carry that group; anyone
// else receives errSecMissingEntitlement from the very first query.
bool canUseSafariKeychainGroup() {
    static const bool ok = [] {
        NSDictionary *q = @{(id)kSecClass: (id)kSecClassInternetPassword,
                            (id)kSecAttrAccessGroup: @"com.apple.cfnetwork",
                            (id)kSecUseDataProtectionKeychain: @YES,
                            (id)kSecMatchLimit: (id)kSecMatchLimitOne,
                            (id)kSecReturnAttributes: @YES};
        CFTypeRef out = nullptr;
        const OSStatus st = SecItemCopyMatching((CFDictionaryRef)q, &out);
        if (out) CFRelease(out);
        qCDebug(lcPasswords) << "com.apple.cfnetwork access group probe:" << st;
        return st != kMissingEntitlement && st != errSecInteractionNotAllowed;
    }();
    return ok;
}

// The data-protection keychain needs an application-identifier entitlement
// (any Developer ID / development signature). Ad-hoc builds fall back to the
// file-based login keychain.
bool dataProtectionKeychainUsable() {
    static const bool ok = [] {
        // A read probe returns errSecItemNotFound even when writes are
        // forbidden, so probe with a throwaway add + delete instead.
        NSMutableDictionary *add = [@{(id)kSecClass: (id)kSecClassInternetPassword,
                                      (id)kSecAttrSecurityDomain: kOwnService,
                                      (id)kSecAttrServer: @"capability-probe.pocb.invalid",
                                      (id)kSecAttrAccount: @"probe",
                                      (id)kSecUseDataProtectionKeychain: @YES,
                                      (id)kSecValueData: [NSData data]} mutableCopy];
        const OSStatus st = SecItemAdd((CFDictionaryRef)add, nullptr);
        if (st == errSecSuccess || st == errSecDuplicateItem) {
            [add removeObjectForKey:(id)kSecValueData];
            SecItemDelete((CFDictionaryRef)add);
        }
        qCDebug(lcPasswords) << "data-protection keychain probe:" << st;
        return st == errSecSuccess || st == errSecDuplicateItem;
    }();
    return ok;
}

NSMutableDictionary *ownBaseQuery(const QString &host, const QString &user) {
    NSMutableDictionary *q = [@{(id)kSecClass: (id)kSecClassInternetPassword,
                                (id)kSecAttrSecurityDomain: kOwnService,
                                (id)kSecAttrServer: host.toNSString()} mutableCopy];
    if (!user.isEmpty()) q[(id)kSecAttrAccount] = user.toNSString();
    if (dataProtectionKeychainUsable()) {
        q[(id)kSecUseDataProtectionKeychain] = @YES;
        q[(id)kSecAttrSynchronizable] = (id)kSecAttrSynchronizableAny;
    }
    return q;
}

QStringList ownUsernames(const QString &host) {
    NSMutableDictionary *q = ownBaseQuery(host, QString());
    q[(id)kSecMatchLimit] = (id)kSecMatchLimitAll;
    q[(id)kSecReturnAttributes] = @YES;
    CFTypeRef out = nullptr;
    const OSStatus st = SecItemCopyMatching((CFDictionaryRef)q, &out);
    QStringList users;
    if (st == errSecSuccess && out) {
        for (NSDictionary *attrs in (__bridge NSArray *)out) {
            NSString *acct = attrs[(id)kSecAttrAccount];
            if (acct.length) users << QString::fromNSString(acct);
        }
    }
    if (out) CFRelease(out);
    users.removeDuplicates();
    return users;
}

QList<PasswordCredential> ownCredentials(const QString &host) {
    QList<PasswordCredential> result;
    // The legacy keychain refuses kSecReturnData with kSecMatchLimitAll, so
    // fetch the secret per account.
    for (const QString &user : ownUsernames(host)) {
        NSMutableDictionary *q = ownBaseQuery(host, user);
        q[(id)kSecMatchLimit] = (id)kSecMatchLimitOne;
        q[(id)kSecReturnData] = @YES;
        CFTypeRef out = nullptr;
        const OSStatus st = SecItemCopyMatching((CFDictionaryRef)q, &out);
        if (st == errSecSuccess && out) {
            NSData *data = (__bridge NSData *)out;
            NSString *pw = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
            result.push_back({host, user, QString::fromNSString(pw ?: @"")});
        }
        if (out) CFRelease(out);
    }
    return result;
}

OSStatus ownSave(const QString &host, const QString &user, const QString &password, bool https) {
    NSData *secret = [password.toNSString() dataUsingEncoding:NSUTF8StringEncoding];
    NSMutableDictionary *q = ownBaseQuery(host, user);
    NSDictionary *update = @{(id)kSecValueData: secret};
    OSStatus st = SecItemUpdate((CFDictionaryRef)q, (CFDictionaryRef)update);
    if (st == errSecSuccess) return st;
    if (st != errSecItemNotFound) qCDebug(lcPasswords) << "SecItemUpdate:" << st;

    NSMutableDictionary *add = ownBaseQuery(host, user);
    [add removeObjectForKey:(id)kSecAttrSynchronizable];
    add[(id)kSecAttrProtocol] = https ? (id)kSecAttrProtocolHTTPS : (id)kSecAttrProtocolHTTP;
    add[(id)kSecAttrAuthenticationType] = (id)kSecAttrAuthenticationTypeHTMLForm;
    add[(id)kSecAttrLabel] = [kOwnLabelPrefix stringByAppendingString:host.toNSString()];
    add[(id)kSecAttrDescription] = @"Web form password";
    add[(id)kSecValueData] = secret;
    if (dataProtectionKeychainUsable()) {
        // Sync through iCloud Keychain when the account allows it.
        add[(id)kSecAttrSynchronizable] = @YES;
        add[(id)kSecAttrAccessible] = (id)kSecAttrAccessibleWhenUnlocked;
    }
    st = SecItemAdd((CFDictionaryRef)add, nullptr);
    if (st == errSecDuplicateItem) st = SecItemUpdate((CFDictionaryRef)q, (CFDictionaryRef)update);
    return st;
}

// ---- SafariShared (WBSSavedAccountStore) ----------------------------------

id safariAccountStore() {
    if (!canUseSafariKeychainGroup()) return nil;
    if (!loadSafariShared()) return nil;
    Class cls = NSClassFromString(@"WBSSavedAccountStore");
    SEL shared = NSSelectorFromString(@"sharedStore");
    if (!cls || ![cls respondsToSelector:shared]) return nil;
    return msgSend<id>((id)cls, shared);
}

bool hostMatchesAccount(const QString &host, id account) {
    SEL sitesSel = NSSelectorFromString(@"sitesAndAdditionalSites");
    if (!responds(account, sitesSel)) sitesSel = NSSelectorFromString(@"sites");
    if (responds(account, sitesSel)) {
        id sites = msgSend<id>(account, sitesSel);
        if ([sites isKindOfClass:[NSArray class]]) {
            for (id s in (NSArray *)sites) {
                NSString *str = [s isKindOfClass:[NSString class]] ? (NSString *)s : [s description];
                if (QString::fromNSString(str).contains(host, Qt::CaseInsensitive)) return true;
            }
        }
    }
    SEL hld = NSSelectorFromString(@"highLevelDomain");
    if (responds(account, hld)) {
        id d = msgSend<id>(account, hld);
        if ([d isKindOfClass:[NSString class]]) {
            const QString dom = QString::fromNSString((NSString *)d).toLower();
            if (!dom.isEmpty() && (host == dom || host.endsWith(QLatin1Char('.') + dom))) return true;
        }
    }
    return false;
}

QList<PasswordCredential> safariCredentials(const QString &host) {
    QList<PasswordCredential> result;
    id store = safariAccountStore();
    SEL all = NSSelectorFromString(@"savedAccountsWithPasswords");
    if (!responds(store, all)) return result;
    id accounts = msgSend<id>(store, all);
    if (![accounts isKindOfClass:[NSArray class]]) return result;
    SEL userSel = NSSelectorFromString(@"user");
    SEL passSel = NSSelectorFromString(@"password");
    for (id acct in (NSArray *)accounts) {
        if (!responds(acct, userSel) || !responds(acct, passSel)) continue;
        if (!hostMatchesAccount(host, acct)) continue;
        id u = msgSend<id>(acct, userSel);
        id p = msgSend<id>(acct, passSel);
        if (![u isKindOfClass:[NSString class]] || ![p isKindOfClass:[NSString class]]) continue;
        result.push_back({host, QString::fromNSString(u), QString::fromNSString(p)});
    }
    return result;
}

bool safariSave(const QString &host, const QString &user, const QString &password, bool https) {
    id store = safariAccountStore();
    SEL sel = NSSelectorFromString(@"saveUser:password:forProtectionSpace:highLevelDomain:groupID:");
    if (!responds(store, sel)) return false;
    NSURLProtectionSpace *space = [[NSURLProtectionSpace alloc] initWithHost:host.toNSString()
                                                                        port:https ? 443 : 80
                                                                    protocol:https ? NSURLProtectionSpaceHTTPS : NSURLProtectionSpaceHTTP
                                                                       realm:nil
                                                        authenticationMethod:NSURLAuthenticationMethodHTMLForm];
    const QStringList parts = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QString hld = parts.size() >= 2 ? parts.mid(parts.size() - 2).join(QLatin1Char('.')) : host;
    id saved = msgSend<id>(store, sel, user.toNSString(), password.toNSString(), space, hld.toNSString(), (id)nil);
    return saved != nil;
}

// ---- AuthenticationServicesCore (ASCAgentProxy) ---------------------------

// Asks AuthenticationServicesAgent for a password credential the way WebKit
// does for Safari. The agent only honours this from processes carrying an
// application identifier plus com.apple.developer.web-browser; otherwise it
// answers "process without application identifier" and we fall through.
void ascFetch(const QString &host, std::function<void(QList<PasswordCredential>, bool handled)> done) {
    if (!loadASCore()) { done({}, false); return; }
    Class ctxClass = NSClassFromString(@"ASCCredentialRequestContext");
    Class proxyClass = NSClassFromString(@"ASCAgentProxy");
    SEL initSel = NSSelectorFromString(@"initWithRequestTypes:");
    SEL rpSel = NSSelectorFromString(@"setRelyingPartyIdentifier:");
    SEL perform = NSSelectorFromString(@"performAutoFillAuthorizationRequestsForContext:withCompletionHandler:");
    if (!ctxClass || !proxyClass || ![ctxClass instancesRespondToSelector:initSel]
        || ![ctxClass instancesRespondToSelector:rpSel] || ![proxyClass instancesRespondToSelector:perform]) {
        done({}, false);
        return;
    }
    constexpr NSUInteger kPasswordAssertion = 1;  // ASCCredentialRequestTypePasswordAssertion
    id ctx = msgSend<id>([ctxClass alloc], initSel, kPasswordAssertion);
    if (!ctx) { done({}, false); return; }
    msgSend<void>(ctx, rpSel, host.toNSString());
    id proxy = [[proxyClass alloc] init];
    if (!proxy) { done({}, false); return; }

    __block bool finished = false;
    auto finish = [done, host](id credential, NSError *error) {
        QList<PasswordCredential> creds;
        if (credential && !error) {
            SEL userSel = NSSelectorFromString(@"user");
            SEL passSel = NSSelectorFromString(@"password");
            if (responds(credential, userSel) && responds(credential, passSel)) {
                id u = msgSend<id>(credential, userSel);
                id p = msgSend<id>(credential, passSel);
                if ([u isKindOfClass:[NSString class]] && [p isKindOfClass:[NSString class]])
                    creds.push_back({host, QString::fromNSString(u), QString::fromNSString(p)});
            }
        } else if (error) {
            qCDebug(lcPasswords) << "ASCAgentProxy:" << QString::fromNSString(error.description);
        }
        done(creds, !creds.isEmpty());
    };
    void (^completion)(id, NSError *) = ^(id credential, NSError *error) {
        if (finished) return;
        finished = true;
        finish(credential, error);
    };
    // Guard against the agent never answering an unentitled caller.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(4 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        (void)proxy;  // keep the XPC proxy and context alive until the agent answers
        (void)ctx;
        if (finished) return;
        finished = true;
        finish(nil, [NSError errorWithDomain:@"pocb.passwords" code:-1 userInfo:@{NSLocalizedDescriptionKey: @"timed out"}]);
    });
    msgSend<void>(proxy, perform, ctx, completion);
}

// ---- password generation --------------------------------------------------

QString localStrongPassword() {
    // Apple's more-typable shape: three six-character groups of lowercase
    // letters and digits joined by hyphens, with at least one digit and one
    // uppercase letter so common complexity rules pass.
    static const QString alphabet = QStringLiteral("abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789");
    auto *rng = QRandomGenerator::system();
    QString out;
    for (;;) {
        out.clear();
        for (int g = 0; g < 3; ++g) {
            if (g) out += QLatin1Char('-');
            for (int i = 0; i < 6; ++i) out += alphabet.at(int(rng->bounded(quint32(alphabet.size()))));
        }
        bool digit = false, upper = false, lower = false;
        for (const QChar c : out) { digit |= c.isDigit(); upper |= c.isUpper(); lower |= c.isLower(); }
        if (digit && upper && lower) return out;
    }
}

QString safariGeneratedPassword(const QUrl &site) {
    if (!loadSafariShared()) return {};
    Class cls = NSClassFromString(@"WBSPasswordGenerationManager");
    SEL initSel = NSSelectorFromString(@"initWithPasswordRequirementsByDomain:passwordRulesByDomain:");
    SEL reqSel = NSSelectorFromString(@"defaultRequirementsForURL:");
    SEL genSel = NSSelectorFromString(@"generatedPasswordMatchingRequirements:");
    if (!cls || ![cls instancesRespondToSelector:initSel] || ![cls instancesRespondToSelector:genSel]) return {};
    id mgr = msgSend<id>([cls alloc], initSel, @{}, @{});
    if (!mgr) return {};
    id requirements = nil;
    if (site.isValid() && [mgr respondsToSelector:reqSel]) requirements = msgSend<id>(mgr, reqSel, site.toNSURL());
    id pw = msgSend<id>(mgr, genSel, requirements);
    if (![pw isKindOfClass:[NSString class]]) return {};
    return QString::fromNSString((NSString *)pw);
}

QString securityGeneratedPassword() {
    using Fn = CFStringRef (*)(void);
    static Fn fn = reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, "SecCreateSharedWebCredentialPassword"));
    if (!fn) return {};
    CFStringRef pw = fn();
    if (!pw) return {};
    QString out = QString::fromCFString(pw);
    CFRelease(pw);
    return out;
}

// ---- Touch ID gate --------------------------------------------------------

void authenticate(const QString &reason, std::function<void(bool ok, QString error)> done) {
    LAContext *ctx = [[LAContext alloc] init];
    ctx.localizedFallbackTitle = @"Use Password…";
    NSError *err = nil;
    LAPolicy policy = LAPolicyDeviceOwnerAuthenticationWithBiometrics;
    if (![ctx canEvaluatePolicy:policy error:&err]) {
        qCDebug(lcPasswords) << "biometrics unavailable:" << (err ? QString::fromNSString(err.localizedDescription) : QString());
        policy = LAPolicyDeviceOwnerAuthentication;
        err = nil;
        if (![ctx canEvaluatePolicy:policy error:&err]) {
            // No passcode/biometrics configured at all: nothing to gate with.
            runOnMain([done] { done(true, QString()); });
            return;
        }
    }
    [ctx evaluatePolicy:policy localizedReason:reason.toNSString() reply:^(BOOL success, NSError *error) {
        (void)ctx;
        const QString msg = error ? QString::fromNSString(error.localizedDescription) : QString();
        runOnMain([done, success, msg] { done(success, msg); });
    }];
}

QString csvEscape(const QString &value) {
    QString v = value;
    v.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + v + QLatin1Char('"');
}

}  // namespace

// ---------------------------------------------------------------------------

ApplePasswords &ApplePasswords::instance() {
    static ApplePasswords s;
    return s;
}

ApplePasswords::ApplePasswords() = default;

bool ApplePasswords::isAvailable() const { return true; }

QString ApplePasswords::backendDescription() const {
    if (canUseSafariKeychainGroup() && safariAccountStore()) return QStringLiteral("Apple Passwords (iCloud Keychain)");
    if (dataProtectionKeychainUsable()) return QStringLiteral("iCloud Keychain (pocb items)");
    return QStringLiteral("Keychain (login keychain)");
}

void ApplePasswords::setRequireBiometrics(bool require) { m_requireBiometrics = require; }
bool ApplePasswords::requireBiometrics() const { return m_requireBiometrics; }

QString ApplePasswords::siteKey(const QUrl &url) {
    QString host = url.host(QUrl::FullyEncoded).toLower();
    if (host.startsWith(QStringLiteral("www."))) host.remove(0, 4);
    return host;
}

QStringList ApplePasswords::savedUsernames(const QUrl &site) const {
    const QString host = siteKey(site);
    if (host.isEmpty()) return {};
    QStringList users = ownUsernames(host);
    if (canUseSafariKeychainGroup()) {
        for (const PasswordCredential &c : safariCredentials(host)) users << c.user;
    }
    users.removeDuplicates();
    return users;
}

void ApplePasswords::fetchCredentials(const QUrl &site, FetchCallback done) {
    const QString host = siteKey(site);
    if (host.isEmpty()) { runOnMain([done] { done({}, QStringLiteral("no host")); }); return; }

    auto load = [host, done] {
        ascFetch(host, [host, done](QList<PasswordCredential> asc, bool handled) {
            QList<PasswordCredential> creds = handled ? asc : QList<PasswordCredential>();
            if (creds.isEmpty()) creds = safariCredentials(host);
            if (creds.isEmpty()) creds = ownCredentials(host);
            runOnMain([done, creds] { done(creds, QString()); });
        });
    };

    if (!m_requireBiometrics) { load(); return; }
    authenticate(QStringLiteral("fill your password for %1").arg(host), [load, done](bool ok, QString error) {
        if (!ok) { done({}, error.isEmpty() ? QStringLiteral("authentication cancelled") : error); return; }
        load();
    });
}

void ApplePasswords::saveCredential(const QUrl &site, const QString &user, const QString &password, BoolCallback done) {
    const QString host = siteKey(site);
    const bool https = site.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
    auto finish = [done](bool ok, const QString &err) { runOnMain([done, ok, err] { if (done) done(ok, err); }); };
    if (host.isEmpty() || user.isEmpty() || password.isEmpty()) { finish(false, QStringLiteral("incomplete credential")); return; }

    // 1. Safari's shared store writes straight into Apple Passwords.
    if (canUseSafariKeychainGroup() && safariSave(host, user, password, https)) { finish(true, QString()); return; }

    // 2. Shared web credentials (macOS reports unimpErr, kept for future OSes).
    using AddFn = void (*)(CFStringRef, CFStringRef, CFStringRef, void (^)(CFErrorRef));
    static AddFn addShared = reinterpret_cast<AddFn>(dlsym(RTLD_DEFAULT, "SecAddSharedWebCredential"));
    if (addShared && canUseSafariKeychainGroup()) {
        __block bool called = false;
        addShared(host.toCFString(), user.toCFString(), password.toCFString(), ^(CFErrorRef error) {
            called = true;
            if (error) qCDebug(lcPasswords) << "SecAddSharedWebCredential:" << QString::fromNSString(((__bridge NSError *)error).description);
        });
        (void)called;
    }

    // 3. pocb's own keychain item (iCloud-synced when the build is entitled).
    const OSStatus st = ownSave(host, user, password, https);
    if (st == errSecSuccess) { finish(true, QString()); return; }
    finish(false, QStringLiteral("keychain error %1").arg(st));
}

bool ApplePasswords::removeCredential(const QUrl &site, const QString &user) {
    const QString host = siteKey(site);
    if (host.isEmpty() || user.isEmpty()) return false;
    NSMutableDictionary *q = ownBaseQuery(host, user);
    return SecItemDelete((CFDictionaryRef)q) == errSecSuccess;
}

QString ApplePasswords::generatePassword(const QUrl &site) {
    QString pw = safariGeneratedPassword(site);
    if (pw.size() >= 12) return pw;
    pw = securityGeneratedPassword();
    if (pw.size() >= 12) return pw;
    return localStrongPassword();
}

bool ApplePasswords::credentialExchangeAvailable() {
    // ASCredentialExportManager is Swift-only and the agent additionally
    // requires the exporter to ship a credential-provider extension, so the
    // Objective-C++ bridge can only report whether a future ObjC surface
    // exists. Both names are probed so an eventual bridge is picked up.
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 26)) return false;
    Class cls = NSClassFromString(@"ASCredentialExportManager");
    if (!cls) cls = NSClassFromString(@"AuthenticationServices.ASCredentialExportManager");
    if (!cls) return false;
    return [cls instancesRespondToSelector:NSSelectorFromString(@"exportCredentials:completionHandler:")]
        && [cls instancesRespondToSelector:NSSelectorFromString(@"initWithPresentationAnchor:")];
}

bool ApplePasswords::openPasswordsApp() {
    NSURL *app = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:@"com.apple.Passwords"];
    if (!app) return false;
    NSWorkspaceOpenConfiguration *cfg = [NSWorkspaceOpenConfiguration configuration];
    cfg.activates = YES;
    [[NSWorkspace sharedWorkspace] openApplicationAtURL:app configuration:cfg completionHandler:nil];
    return true;
}

bool ApplePasswords::writeChromeCsv(const QString &path, const QList<PasswordCredential> &credentials, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QTextStream out(&f);
    out << "name,url,username,password,note\n";
    for (const PasswordCredential &c : credentials) {
        const QString url = c.site.contains(QStringLiteral("://")) ? c.site : QStringLiteral("https://") + c.site;
        out << csvEscape(c.site) << ',' << csvEscape(url) << ',' << csvEscape(c.user) << ',' << csvEscape(c.password) << ",\"\"\n";
    }
    out.flush();
    return f.error() == QFileDevice::NoError;
}

void ApplePasswords::exportToPasswordsApp(const QList<PasswordCredential> &credentials, QWidget *parent, BoolCallback done) {
    Q_UNUSED(parent);
    auto finish = [done](bool ok, const QString &msg) { runOnMain([done, ok, msg] { if (done) done(ok, msg); }); };
    if (credentials.isEmpty()) { finish(false, QStringLiteral("Nothing to export.")); return; }

    if (credentialExchangeAvailable()) {
        // A future ObjC-visible Credential Exchange surface would be driven
        // here; until then the check above keeps this branch unreachable.
        qCInfo(lcPasswords) << "Credential Exchange bridge detected but no verified call path; using fallback";
    }

    // Fallback: store each credential into the keychain backend we do have so
    // pocb can autofill it, and report that the Passwords app import needs the
    // CSV route (handled by PasswordImport which owns the file dialog).
    int saved = 0;
    for (const PasswordCredential &c : credentials) {
        QUrl u = c.site.contains(QStringLiteral("://")) ? QUrl(c.site) : QUrl(QStringLiteral("https://") + c.site);
        const QString host = siteKey(u);
        if (host.isEmpty() || c.user.isEmpty() || c.password.isEmpty()) continue;
        const bool https = u.scheme() != QStringLiteral("http");
        if ((canUseSafariKeychainGroup() && safariSave(host, c.user, c.password, https))
            || ownSave(host, c.user, c.password, https) == errSecSuccess) ++saved;
    }
    finish(saved > 0, QStringLiteral("%1 of %2 logins stored in %3.").arg(saved).arg(credentials.size()).arg(backendDescription()));
}

}  // namespace mac
