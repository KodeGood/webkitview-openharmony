#!/bin/bash

set -Eeuo pipefail

die(){ echo "ERROR: $*" >&2; exit 2; }
say(){ printf "\n==> %s\n" "$*"; }
need(){ command -v "$1" >/dev/null 2>&1 || die "Missing '$1' in PATH"; }

# ---------- Defaults ----------
BUNDLE_NAME="${3:-${BUNDLE_NAME:-com.kodegood.webkitview}}"
ABILITY_NAME="${4:-${ABILITY_NAME:-EntryAbility}}"

need hdc
HDC_CMD=(hdc)
if [[ -n "${HDC_TARGET:-}" ]]; then HDC_CMD+=( -t "$HDC_TARGET" ); fi

# ---------- Stop the app ----------
say "Stopping previous: ${BUNDLE_NAME}"
hdc shell aa force-stop "$BUNDLE_NAME" > /dev/null 2>&1

hdc file send entry/build/default/intermediates/libs/default/arm64-v8a/libebkitview.so /data/app/el1/bundle/public/com.kodegood.webkitview/libs/arm64/
hdc file send entry/build/default/intermediates/libs/default/arm64-v8a/libebkit_network_process.so /data/app/el1/bundle/public/com.kodegood.webkitview/libs/arm64/
hdc file send entry/build/default/intermediates/libs/default/arm64-v8a/libebkit_web_process.so /data/app/el1/bundle/public/com.kodegood.webkitview/libs/arm64/
