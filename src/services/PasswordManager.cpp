#include "PasswordManager.hpp"

#include "WebView.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSettings>

namespace {

constexpr const char *kEnabledKey = "passwords/applePasswordsEnabled";
constexpr const char *kTouchIDKey = "passwords/autofillRequiresTouchID";
constexpr const char *kNeverSaveKey = "passwords/neverSave";

// Injected at document start into every frame. Everything is wrapped so a
// hostile page cannot break the browser; failures degrade to "no offer".
const char *kContentScript = R"JS(
(function () {
  if (window.__pocbPwInstalled) return;
  window.__pocbPwInstalled = true;
  var post = function (name, body) {
    try { window.webkit.messageHandlers.pocb.postMessage({ name: name, body: body }); } catch (e) {}
  };
  var USER_RE = /user|login|log-in|email|e-mail|mail|account|identifier|phone|tel|uid|name/i;
  var SIGNUP_RE = /sign ?up|register|registration|create (an |your |new )?account|join|get started|new password|choose (a )?password|confirm password|repeat password|reset password|change password/i;
  var visible = function (el) {
    if (!el || !el.getClientRects) return false;
    var r = el.getBoundingClientRect();
    if (r.width <= 0 || r.height <= 0) return false;
    var cs = window.getComputedStyle(el);
    return cs.visibility !== 'hidden' && cs.display !== 'none';
  };
  var all = function (sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); };
  var passwordFields = function (root) { return all('input[type=password]', root).filter(function (el) { return visible(el) && !el.disabled; }); };
  var isUsernameInput = function (el) {
    if (!(el instanceof HTMLInputElement)) return false;
    var t = (el.type || 'text').toLowerCase();
    if (['text', 'email', 'tel', ''].indexOf(t) < 0) return false;
    if (!visible(el) || el.disabled || el.readOnly) return false;
    var ac = (el.getAttribute('autocomplete') || '').toLowerCase();
    if (/off|one-time-code|cc-|postal|address|search/.test(ac) && !/username|email/.test(ac)) return false;
    return true;
  };
  var findUsername = function (pw) {
    var scope = pw.form || pw.closest('form') || document;
    var inputs = all('input', scope);
    var idx = inputs.indexOf(pw);
    var candidates = inputs.slice(0, idx < 0 ? inputs.length : idx).filter(isUsernameInput);
    if (!candidates.length && scope !== document) candidates = all('input', document).filter(isUsernameInput);
    if (!candidates.length) return null;
    var byAc = candidates.filter(function (el) { return /username|email/.test((el.getAttribute('autocomplete') || '').toLowerCase()); });
    if (byAc.length) return byAc[byAc.length - 1];
    var byName = candidates.filter(function (el) {
      return USER_RE.test(el.name || '') || USER_RE.test(el.id || '') || USER_RE.test(el.placeholder || '') || USER_RE.test(el.getAttribute('aria-label') || '') || el.type === 'email';
    });
    if (byName.length) return byName[byName.length - 1];
    return candidates[candidates.length - 1];
  };
  var describe = function (el, kind) {
    return { kind: kind, type: el.type || '', name: el.name || '', id: el.id || '',
             autocomplete: el.getAttribute('autocomplete') || '', placeholder: el.placeholder || '' };
  };
  var scan = function () {
    var pws = passwordFields(document);
    if (!pws.length) return null;
    var hasNew = false, hasCurrent = false;
    var fields = [];
    var seenUsers = [];
    var formCounts = new Map();
    pws.forEach(function (pw) {
      var ac = (pw.getAttribute('autocomplete') || '').toLowerCase();
      var kind = 'password';
      if (ac.indexOf('new-password') >= 0) { hasNew = true; kind = 'newPassword'; }
      else if (ac.indexOf('current-password') >= 0) { hasCurrent = true; }
      var form = pw.form || pw.closest('form') || document;
      formCounts.set(form, (formCounts.get(form) || 0) + 1);
      var u = findUsername(pw);
      if (u && seenUsers.indexOf(u) < 0) { seenUsers.push(u); fields.push(describe(u, 'username')); }
      fields.push(describe(pw, kind));
    });
    formCounts.forEach(function (count, form) {
      if (count >= 2) {
        hasNew = true;
      } else if (!hasNew && !hasCurrent) {
        var text = '';
        if (form !== document) {
          text = (form.innerText || '') + ' ' + (form.getAttribute('action') || '') + ' ' + (form.id || '') + ' ' + (form.className || '');
        } else {
          text = document.title + ' ' + location.pathname;
        }
        if (SIGNUP_RE.test(text)) hasNew = true;
      }
    });
    if (!hasNew) hasCurrent = true;
    return { origin: location.origin, hasNewPassword: hasNew, hasCurrentPassword: hasCurrent, fields: fields };
  };
  var lastKey = '';
  var timer = null;
  var report = function (force) {
    var info = scan();
    var key = info ? JSON.stringify(info) : '';
    if (!force && key === lastKey) return;
    lastKey = key;
    if (info) post('pw.formsFound', info);
  };
  var schedule = function () {
    if (timer) return;
    timer = setTimeout(function () { timer = null; report(false); }, 350);
  };
  var lastSubmit = { u: '', p: '', t: 0 };
  var capture = function (pw) {
    if (!pw || !pw.value) return;
    var u = findUsername(pw);
    var username = u ? u.value : '';
    var now = Date.now();
    if (lastSubmit.u === username && lastSubmit.p === pw.value && now - lastSubmit.t < 1500) return;
    lastSubmit = { u: username, p: pw.value, t: now };
    post('pw.submitted', { origin: location.origin, username: username, password: pw.value });
  };
  var firstFilledPassword = function (scope) {
    var pws = passwordFields(scope);
    for (var i = 0; i < pws.length; i++) if (pws[i].value) return pws[i];
    return null;
  };
  var install = function () {
    document.addEventListener('submit', function (e) {
      var f = e.target;
      if (!(f instanceof HTMLFormElement)) return;
      capture(firstFilledPassword(f));
    }, true);
    document.addEventListener('keydown', function (e) {
      var t = e.target;
      if (e.key === 'Enter' && t instanceof HTMLInputElement && t.type === 'password' && t.value) capture(t);
    }, true);
    document.addEventListener('click', function (e) {
      var t = e.target;
      if (!t || !t.closest) return;
      var b = t.closest('button, input[type=submit], input[type=button], [role=button], a[href="#"]');
      if (!b) return;
      var scope = b.form || b.closest('form') || document;
      capture(firstFilledPassword(scope));
    }, true);
    if (document.documentElement) {
      new MutationObserver(schedule).observe(document.documentElement, {
        childList: true, subtree: true, attributes: true, attributeFilter: ['type', 'autocomplete', 'style', 'class', 'hidden']
      });
    }
    window.addEventListener('pageshow', function () { report(true); });
    report(true);
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', install);
  else install();

  var setValue = function (el, value) {
    if (!el) return;
    try { el.focus(); } catch (e) {}
    var proto = Object.getPrototypeOf(el);
    var desc = proto ? Object.getOwnPropertyDescriptor(proto, 'value') : null;
    if (desc && desc.set) desc.set.call(el, value); else el.value = value;
    el.dispatchEvent(new Event('input', { bubbles: true }));
    el.dispatchEvent(new Event('change', { bubbles: true }));
    try { el.blur(); } catch (e) {}
  };
  window.__pocbFill = function (user, pass) {
    var pws = passwordFields(document);
    if (!pws.length) return false;
    var u = findUsername(pws[0]);
    if (user && u) setValue(u, user);
    if (pass) pws.forEach(function (p) { setValue(p, pass); });
    return true;
  };
  window.__pocbFillGenerated = function (pass) {
    var pws = passwordFields(document);
    var targets = pws.filter(function (p) { return (p.getAttribute('autocomplete') || '').indexOf('new-password') >= 0; });
    if (!targets.length) {
      var form = pws.length ? (pws[0].form || pws[0].closest('form')) : null;
      var same = form ? pws.filter(function (p) { return (p.form || p.closest('form')) === form; }) : pws;
      targets = same.length >= 2 ? same : pws;
    }
    targets.forEach(function (p) { setValue(p, pass); });
    return targets.length > 0;
  };
})();
)JS";

}  // namespace

PasswordManager *PasswordManager::instance() {
    static PasswordManager *s = new PasswordManager(QCoreApplication::instance());
    return s;
}

PasswordManager::PasswordManager(QObject *parent) : QObject(parent) {
    // Register before any WKWebView configuration exists; settings are read
    // lazily because the startup routine below runs before main() names the
    // application for QSettings.
    WebView::registerUserScript(contentScript(), /*mainFrameOnly*/ false);
    WebView::addNativeWebViewHook([this](void *, WebView *owner) { attach(owner); });
}

bool PasswordManager::isEnabled() const { loadSettings(); return m_enabled; }
bool PasswordManager::autofillRequiresTouchID() const { loadSettings(); return m_requireTouchID; }

void PasswordManager::loadSettings() const {
    if (m_settingsLoaded) return;
    m_settingsLoaded = true;
    QSettings settings;
    m_enabled = settings.value(QLatin1String(kEnabledKey), true).toBool();
    m_requireTouchID = settings.value(QLatin1String(kTouchIDKey), true).toBool();
    const QStringList never = settings.value(QLatin1String(kNeverSaveKey)).toStringList();
    m_neverSave = QSet<QString>(never.begin(), never.end());
    mac::ApplePasswords::instance().setRequireBiometrics(m_requireTouchID);
}

// Instantiate as soon as QCoreApplication exists so the content script is
// part of every WebView created by main().
static void pocbPasswordManagerStartup() { PasswordManager::instance(); }
Q_COREAPP_STARTUP_FUNCTION(pocbPasswordManagerStartup)

QString PasswordManager::contentScript() { return QString::fromUtf8(kContentScript); }

QString PasswordManager::jsString(const QString &s) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{QJsonValue(s)}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

void PasswordManager::attach(WebView *view) {
    if (!view || m_attached.contains(view)) return;
    loadSettings();
    m_attached.insert(view);
    connect(view, &WebView::scriptMessage, this, [this, view](const QString &name, const QVariant &body) {
        handleMessage(view, name, body);
    });
    connect(view, &QObject::destroyed, this, [this, view] {
        m_attached.remove(view);
        m_pages.remove(view);
    });
}

void PasswordManager::handleMessage(WebView *view, const QString &name, const QVariant &body) {
    loadSettings();
    if (!m_enabled) return;
    if (name == QLatin1String("pw.formsFound")) handleFormsFound(view, body.toMap());
    else if (name == QLatin1String("pw.submitted")) handleSubmitted(view, body.toMap());
}

void PasswordManager::handleFormsFound(WebView *view, const QVariantMap &body) {
    const QUrl origin(body.value(QStringLiteral("origin")).toString());
    if (!origin.isValid() || origin.host().isEmpty()) return;
    PageState &st = m_pages[view];
    const bool originChanged = st.origin != origin;
    st.origin = origin;
    st.hasNewPassword = body.value(QStringLiteral("hasNewPassword")).toBool();
    st.hasCurrentPassword = body.value(QStringLiteral("hasCurrentPassword")).toBool();
    if (originChanged) { st.pendingGenerated.clear(); st.lastOfferedUser.clear(); st.lastOfferedPassword.clear(); }

    if (st.hasNewPassword && !isNeverSave(origin)) {
        if (st.pendingGenerated.isEmpty()) st.pendingGenerated = mac::ApplePasswords::instance().generatePassword(origin);
        emit generateAvailable(view, st.pendingGenerated);
    }
    if (st.hasCurrentPassword || !st.hasNewPassword) {
        const QStringList users = mac::ApplePasswords::instance().savedUsernames(origin);
        if (!users.isEmpty()) emit autofillAvailable(view, users);
    }
}

void PasswordManager::handleSubmitted(WebView *view, const QVariantMap &body) {
    const QUrl origin(body.value(QStringLiteral("origin")).toString());
    const QString user = body.value(QStringLiteral("username")).toString().trimmed();
    const QString password = body.value(QStringLiteral("password")).toString();
    if (!origin.isValid() || origin.host().isEmpty() || password.isEmpty()) return;
    if (isNeverSave(origin)) return;
    PageState &st = m_pages[view];
    if (st.origin != origin) { st.origin = origin; st.pendingGenerated.clear(); }
    if (st.lastOfferedUser == user && st.lastOfferedPassword == password) return;
    st.lastOfferedUser = user;
    st.lastOfferedPassword = password;
    // Skip when we just autofilled exactly this credential.
    if (st.lastFilledUser == user && st.lastFilledPassword == password) return;
    emit saveOffered(view, user, password);
}

void PasswordManager::fill(WebView *view, const QString &user) {
    loadSettings();
    if (!view || !m_enabled) return;
    const QUrl site = view->url();
    QPointer<WebView> guard(view);
    mac::ApplePasswords::instance().fetchCredentials(site, [this, guard, user](const QList<mac::PasswordCredential> &creds, const QString &error) {
        if (!guard) return;
        if (creds.isEmpty()) {
            if (!error.isEmpty()) qInfo() << "pocb.passwords: fill failed:" << error;
            return;
        }
        const mac::PasswordCredential *pick = &creds.first();
        for (const mac::PasswordCredential &c : creds) if (!user.isEmpty() && c.user == user) { pick = &c; break; }
        fillInto(guard, pick->user, pick->password);
    });
}

void PasswordManager::fillInto(WebView *view, const QString &user, const QString &password) {
    PageState &st = m_pages[view];
    st.lastFilledUser = user;
    st.lastFilledPassword = password;
    view->runJavaScript(QStringLiteral("window.__pocbFill && window.__pocbFill(%1, %2);").arg(jsString(user), jsString(password)));
}

void PasswordManager::generateAndFill(WebView *view) {
    loadSettings();
    if (!view || !m_enabled) return;
    PageState &st = m_pages[view];
    if (st.pendingGenerated.isEmpty()) st.pendingGenerated = mac::ApplePasswords::instance().generatePassword(view->url());
    view->runJavaScript(QStringLiteral("window.__pocbFillGenerated && window.__pocbFillGenerated(%1);").arg(jsString(st.pendingGenerated)));
}

void PasswordManager::save(const QUrl &site, const QString &user, const QString &password) {
    mac::ApplePasswords::instance().saveCredential(site, user, password, [this, site, user](bool ok, const QString &error) {
        if (!ok) qInfo() << "pocb.passwords: save failed for" << site.host() << ":" << error;
        emit saveFinished(site, user, ok, error);
    });
}

void PasswordManager::refresh(WebView *view) {
    if (!view || !m_enabled) return;
    auto it = m_pages.find(view);
    if (it == m_pages.end()) return;
    const QStringList users = mac::ApplePasswords::instance().savedUsernames(it->origin);
    if (!users.isEmpty()) emit autofillAvailable(view, users);
}

bool PasswordManager::isNeverSave(const QUrl &site) const {
    loadSettings();
    return m_neverSave.contains(mac::ApplePasswords::siteKey(site));
}

void PasswordManager::setNeverSave(const QUrl &site, bool never) {
    loadSettings();
    const QString host = mac::ApplePasswords::siteKey(site);
    if (host.isEmpty()) return;
    if (never) m_neverSave.insert(host); else m_neverSave.remove(host);
    QSettings().setValue(QLatin1String(kNeverSaveKey), QStringList(m_neverSave.begin(), m_neverSave.end()));
}

void PasswordManager::setEnabled(bool enabled) {
    loadSettings();
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    QSettings().setValue(QLatin1String(kEnabledKey), enabled);
    emit enabledChanged(enabled);
}

void PasswordManager::setAutofillRequiresTouchID(bool require) {
    loadSettings();
    m_requireTouchID = require;
    QSettings().setValue(QLatin1String(kTouchIDKey), require);
    mac::ApplePasswords::instance().setRequireBiometrics(require);
}
