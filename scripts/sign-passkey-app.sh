#!/usr/bin/env bash
set -euo pipefail

app_path="${POCB_APP_PATH:-build/pocb.app}"
identity="${POCB_CODESIGN_IDENTITY:-}"
team_id="${POCB_TEAM_ID:-}"
bundle_id="${POCB_BUNDLE_ID:-lol.plyght.pocb}"
# Optional .provisionprofile granting the restricted web-browser entitlements
# (com.apple.developer.web-browser, ...public-key-credential). Without one,
# AMFI refuses to launch a Developer ID build that carries them.
profile="${POCB_PROVISIONING_PROFILE:-}"

if [[ -z "$identity" || -z "$team_id" ]]; then
    # Unsigned/ad-hoc builds still run; they just cannot use passkeys, the
    # data-protection keychain, or Credential Exchange (see docs/apple-passwords.md).
    echo "sign-passkey-app: POCB_CODESIGN_IDENTITY / POCB_TEAM_ID not set; leaving $app_path ad-hoc signed" >&2
    exit 0
fi

if ! /usr/bin/security find-identity -v -p codesigning 2>/dev/null | grep -Fq "$identity"; then
    echo "sign-passkey-app: identity '$identity' is not a valid codesigning identity on this machine; leaving $app_path unsigned" >&2
    exit 0
fi

if [[ ! -d "$app_path" ]]; then
    echo "missing app bundle: $app_path" >&2
    exit 1
fi

if [[ -n "$profile" ]]; then
    if [[ ! -f "$profile" ]]; then
        echo "missing provisioning profile: $profile" >&2
        exit 1
    fi
    cp "$profile" "$app_path/Contents/embedded.provisionprofile"
fi

entitlements="$(mktemp)"
trap 'rm -f "$entitlements"' EXIT
cp pocb.entitlements "$entitlements"
/usr/libexec/PlistBuddy -c "Add :com.apple.application-identifier string ${team_id}.${bundle_id}" "$entitlements"
/usr/libexec/PlistBuddy -c "Add :com.apple.developer.team-identifier string ${team_id}" "$entitlements"
# keychain-access-groups lets pocb's saved logins live in the data-protection
# (iCloud-synced) keychain under the app's own group.
/usr/libexec/PlistBuddy -c "Add :keychain-access-groups array" "$entitlements"
/usr/libexec/PlistBuddy -c "Add :keychain-access-groups:0 string ${team_id}.${bundle_id}" "$entitlements"

/usr/bin/codesign --force --options runtime --timestamp --sign "$identity" --entitlements "$entitlements" "$app_path"
scripts/assert-passkey-entitlement.sh "$app_path"
