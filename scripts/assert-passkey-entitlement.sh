#!/usr/bin/env bash
set -euo pipefail

app_path="${1:-build/pocb.app}"

if [[ ! -d "$app_path" ]]; then
    echo "missing app bundle: $app_path" >&2
    exit 1
fi

entitlements="$(/usr/bin/codesign -d --entitlements :- "$app_path" 2>/dev/null || true)"

if ! grep -q "com.apple.developer.web-browser.public-key-credential" <<<"$entitlements"; then
    echo "missing com.apple.developer.web-browser.public-key-credential entitlement on $app_path" >&2
    exit 1
fi

if ! grep -q "com.apple.application-identifier" <<<"$entitlements"; then
    echo "missing com.apple.application-identifier entitlement on $app_path" >&2
    exit 1
fi

if ! grep -q "com.apple.developer.team-identifier" <<<"$entitlements"; then
    echo "missing com.apple.developer.team-identifier entitlement on $app_path" >&2
    exit 1
fi

printf 'passkey entitlements present on %s\n' "$app_path"
