# Apple Passwords integration

pocb uses Apple Passwords / iCloud Keychain as its built-in password manager.
This document records what was verified on the development machine, what is
inferred, and which paths depend on code-signing entitlements that were not
available during development.

Test environment: macOS 26.5.2 (25F84), Xcode 26.6 (17F113), ad-hoc signed
`build-passwords/pocb.app` (no Developer ID, no provisioning profile,
Touch ID not enrolled on the test Mac).

## Components

| File | Role |
| --- | --- |
| `src/mac/ApplePasswords.hpp/.mm` | Keychain/Passwords backend: fetch, save, generate, export, Touch ID gate. All private APIs are resolved at runtime. |
| `src/services/PasswordManager.hpp/.cpp` | QObject singleton. Injects the form-detection user script, receives `pw.formsFound` / `pw.submitted`, emits `autofillAvailable`, `generateAvailable`, `saveOffered`, `saveFinished`. |
| `src/ui/PasswordPrompt.hpp/.cpp` | Liquid Glass popover anchored under the address pill: saved-accounts, strong-password and save variants. |
| `src/services/PasswordImport.hpp/.cpp` | CSV parser for Chrome/Arc/Brave/Edge, Firefox and Safari/Passwords exports, dedup, `importFromCsvInteractive()`. |
| `pocb.entitlements`, `scripts/sign-passkey-app.sh` | Browser entitlements and the (optional) Developer ID signing step. |

Settings (QSettings, `com.plyght.pocb`):

| Key | Default | Meaning |
| --- | --- | --- |
| `passwords/applePasswordsEnabled` | `true` | Master switch. When off, no script messages are handled and no prompts are emitted. |
| `passwords/autofillRequiresTouchID` | `true` | Gate secret reads behind `LAContext`. |
| `passwords/neverSave` | `[]` | Hosts for which "Never for this Site" was chosen. |

## Backend selection (verified)

`ApplePasswords` probes capabilities once at startup and picks the first
usable backend:

1. **Apple's own store** (`WBSSavedAccountStore` from SafariShared, keychain
   access group `com.apple.cfnetwork`). Requires an Apple-private entitlement.
   Probe result on this machine: `-34018 errSecMissingEntitlement`. Never
   used unless the probe succeeds, because unentitled access to that store
   logs `SecItemAdd failed with error -34018` and returns bogus results.
2. **Data-protection (iCloud-synced) keychain**, pocb's own items
   (`kSecAttrSecurityDomain = pocb.apple-passwords`,
   `kSecAttrSynchronizable = YES`). Requires an `application-identifier`
   entitlement, i.e. any real signing identity plus `keychain-access-groups`
   (added by `scripts/sign-passkey-app.sh`). Probe result on the ad-hoc build:
   `-34018`. The probe is a throwaway `SecItemAdd`/`SecItemDelete`, because a
   read probe misleadingly returns `-25300` even when writes are forbidden.
3. **Legacy login keychain** (file based, `~/Library/Keychains`). Works for
   ad-hoc builds. Verified end to end: save from
   `https://the-internet.herokuapp.com/login` (`tomsmith`), item visible via
   `security find-internet-password -s the-internet.herokuapp.com`
   (`sdmn = pocb.apple-passwords`), then autofilled on the next visit and the
   login succeeded.

`backendDescription()` reports which one is active so the settings UI can
show "Apple Passwords (iCloud Keychain)", "iCloud Keychain (pocb items)" or
"Keychain (login keychain)".

Consequence: in the ad-hoc/dev build pocb stores logins in the user's login
keychain, **not** in the Passwords app database. They are pocb-only until
the build is signed with a real identity (then they sync via iCloud Keychain
under pocb's access group) or an Apple entitlement grants access to
`com.apple.cfnetwork` (then they appear in Passwords.app directly).

## Password generation (verified)

Order: SafariShared SPI, then Security.framework, then a local generator.

- `WBSPasswordGenerationManager` (`/System/Library/PrivateFrameworks/SafariShared.framework`),
  `initWithPasswordRequirementsByDomain:passwordRulesByDomain:` with empty
  dictionaries, then `generatedPasswordMatchingRequirements:` /
  `defaultRequirementsForURL:`. Verified in an unsigned CLI probe and inside
  pocb: produces Safari-style passwords such as `Qephem-6cytwi-kukvaq`,
  `Gydce5-vixfaj-dekpyw`, `fajhi9-tabTez-xawsaf`.
- `SecCreateSharedWebCredentialPassword()` (public, `dlsym`-resolved):
  verified, returns e.g. `MkF-a6b-FQ9-C4p`.
- Local fallback: 20 chars in `xxxxxx-xxxxxx-xxxxxx` form with at least one
  upper, lower and digit, drawn from `QRandomGenerator::system()`.

## Touch ID gate (verified, fallback path)

`fetchCredentials` first evaluates `LAPolicyDeviceOwnerAuthenticationWithBiometrics`.
If `canEvaluatePolicy:` fails (this Mac: "Biometry is not enrolled.") it
falls back to `LAPolicyDeviceOwnerAuthentication`, which was verified to show
the system password sheet ("pocb is trying to fill your password for
the-internet.herokuapp.com"). Cancelling yields `fill failed:
"Authentication canceled."` and nothing is filled. The biometric branch
itself could not be exercised because no fingerprint is enrolled. Only
usernames are read without authentication (needed to populate the prompt).

## Form detection and filling (verified)

The document-start user script (all frames) posts
`{name:"pw.formsFound", body:{origin, hasNewPassword, hasCurrentPassword, fields}}`
on `DOMContentLoaded`, `pageshow` and mutations (350 ms throttle), and
`{name:"pw.submitted", body:{origin, username, password}}` from a
capture-phase `submit` listener plus Enter/click fallbacks. `window.__pocbFill`
and `window.__pocbFillGenerated` set values and dispatch `input`/`change`.

Verified:
- `the-internet.herokuapp.com/login`: `saveOffered` after submit, save prompt
  shown, save succeeded, `autofillAvailable(["tomsmith"])` on the next load,
  fill populated both fields, login reached `/secure`, no duplicate save offer
  for the just-filled credential.
- Sign-up page with two `autocomplete="new-password"` inputs (served from
  `http://localhost:8765`): `generateAvailable` fired, "Use Strong Password"
  prompt shown, "Use" filled both password fields, submit produced a
  `saveOffered` for the generated password.
- `file://` pages are ignored on purpose (`location.origin` is `null`).

Not verified: iframe login forms, SPA logins without a `<form>` element
(covered by the click/Enter fallbacks, untested), sites that re-render the
inputs after fill (`input`/`change` are dispatched but React-style value
trackers are not patched).

## Passkeys / WebAuthn (verified: blocked by signing)

WKWebView already routes `navigator.credentials.create()` to the system
`ASAuthorizationController`. On the ad-hoc build `https://webauthn.io`
Register fails with `NotAllowedError` and the unified log shows:

```
AuthenticationServicesAgent: Attempted to perform authorization from process without application identifier.
pocb: ASAuthorizationController credential request failed with error: ...AuthorizationError Code=1004
```

So passkeys need a real signature carrying `com.apple.application-identifier`
plus `com.apple.developer.web-browser.public-key-credential` (restricted:
needs a provisioning profile from Apple). `pocb.entitlements` now also lists
`com.apple.developer.web-browser`; `AuthenticationServicesCore` checks that
string before treating a client as a browser (seen in the binary's strings;
inferred, not exercised). Ad-hoc signing with either restricted entitlement
makes AMFI refuse to launch the app ("Adhoc signed app with restricted
entitlements detected"), which is why the CMake build does not apply
`pocb.entitlements` and `scripts/sign-passkey-app.sh` is a separate,
opt-in step that exits 0 when no identity is configured.

`security find-identity -v -p codesigning` returned `0 valid identities` on
this machine, so the native Touch ID passkey sheet could not be reached.
Sign in with Apple JS flows work like any other web login; the native
`ASAuthorizationAppleIDProvider` path needs `com.apple.developer.applesignin`
and was not attempted.

## Credential Exchange / Passwords import (partially verified)

macOS 26 exposes the public Swift-only `ASCredentialExportManager` /
`ASCredentialImportManager`. A standalone Swift probe built
`ASExportedCredentialData` fine but `requestExport` failed with
`NSCocoaErrorDomain 4099` and the agent logged
`Rejecting connection from unentitled process`. There is no Objective-C
surface, so `ApplePasswords::credentialExchangeAvailable()` runtime-probes
`ASCredentialExportManager` and stays `false` today.

What `PasswordImport::importFromCsvInteractive()` does instead (verified with
a parser harness, the dialog flow was not driven by automation):

1. `QFileDialog` for a `.csv`.
2. Parse (RFC 4180, BOM, quoted commas/quotes/newlines), detect the layout
   (Chromium `name,url,username,password,note`; Firefox
   `url,username,password,httpRealm,...`; Safari
   `Title,URL,Username,Password,Notes,OTPAuth`), drop rows without
   url/user/password, dedupe on lowercase host + case-insensitive user
   (last non-empty password wins).
3. Store everything through `ApplePasswords::exportToPasswordsApp`, which
   uses the active backend above so pocb can autofill immediately.
4. Offer to write a Chrome-format CSV (`writeChromeCsv`, mode 0600) and open
   Passwords.app so the user can File > Import Passwords… it. This is the
   only path into the Passwords database that works without entitlements.

Because the project now has an optional Swift target (`pocb_intelligence`),
a future Swift bridge could call `ASCredentialExportManager` directly once
the app is signed; `credentialExchangeAvailable()` is the hook for that.

## Private API safety

Every non-public symbol is resolved at runtime: `dlopen` of
`SafariShared.framework` and `AuthenticationServicesCore.framework`,
`NSClassFromString`, `respondsToSelector:` / `instancesRespondToSelector:`
before every `objc_msgSend`, `dlsym(RTLD_DEFAULT, ...)` for Security
functions. Missing frameworks, classes, selectors, entitlements and keychain
groups all degrade to the next backend; `ASCAgentProxy` calls carry a 4 s
timeout because an unentitled agent connection may never answer. The
observed unentitled reply is `AuthenticationServicesCore.AuthorizationError
Code=1`, handled as "no credentials".

## Manual test recipe

```bash
cmake -S . -B build-passwords -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-passwords
mkdir -p ~/sandbox-passwords
HOME=~/sandbox-passwords POCB_PASSWORDS_SELFTEST=1 QT_LOGGING_RULES="pocb.passwords.debug=true" \
  build-passwords/pocb.app/Contents/MacOS/pocb
```

`POCB_PASSWORDS_SELFTEST=1` enables a temporary presenter at the bottom of
`src/ui/PasswordPrompt.cpp` (`TEMP-TEST-BEGIN`/`END`) that connects the
manager to the prompts without BrowserWindow. A private `$HOME` has no login
keychain; create one first with `security create-keychain` and
`security default-keychain -s`, otherwise the legacy backend shows a
"Keychain Not Found" dialog (`-60006`). Note that QSettings go through
`cfprefsd` and ignore `$HOME`.
