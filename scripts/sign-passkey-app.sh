#!/usr/bin/env bash
set -euo pipefail

app_path="${POCB_APP_PATH:-build/pocb.app}"
identity="${POCB_CODESIGN_IDENTITY:-}"
team_id="${POCB_TEAM_ID:-}"
bundle_id="${POCB_BUNDLE_ID:-lol.plyght.pocb}"

if [[ -z "$identity" || -z "$team_id" ]]; then
    echo "set POCB_CODESIGN_IDENTITY and POCB_TEAM_ID" >&2
    exit 1
fi

if [[ ! -d "$app_path" ]]; then
    echo "missing app bundle: $app_path" >&2
    exit 1
fi

entitlements="$(mktemp)"
trap 'rm -f "$entitlements"' EXIT
cp pocb.entitlements "$entitlements"
/usr/libexec/PlistBuddy -c "Add :com.apple.application-identifier string ${team_id}.${bundle_id}" "$entitlements"
/usr/libexec/PlistBuddy -c "Add :com.apple.developer.team-identifier string ${team_id}" "$entitlements"

/usr/bin/codesign --force --options runtime --timestamp --sign "$identity" --entitlements "$entitlements" "$app_path"
scripts/assert-passkey-entitlement.sh "$app_path"
