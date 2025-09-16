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

hdc shell 'ls -t /data/log/faultlog/faultlogger/cppcrash-com.kodegood.webkitview-* | head -n 1' | xargs -I{} hdc file recv {} .
