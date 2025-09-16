#!/bin/bash
set -Eeuo pipefail

die(){ echo "ERROR: $*" >&2; exit 2; }
say(){ printf "\n==> %s\n" "$*"; }
need(){ command -v "$1" >/dev/null 2>&1 || die "Missing '$1' in PATH"; }

# ---------- Defaults ----------
LLVM_STRIP_BIN="${LLVM_STRIP:-llvm-strip}"
need "$LLVM_STRIP_BIN"

SRC_LIB="../webkit-openharmony-cerbero/build/dist/ohos_arm64/lib/libWPEWebKit-2.0.so"
DEST_DIR=".webkit/current/arm64-v8a/sdk/lib/"
DEST_NAME="$(basename "$SRC_LIB")"
TMPFILE="$(mktemp -t "${DEST_NAME}.XXXXXX")"

# Ensure temp file is cleaned up
cleanup(){ rm -f "$TMPFILE"; }
trap cleanup EXIT

# ---------- Strip + copy ----------
say "Stripping ${SRC_LIB}"
"$LLVM_STRIP_BIN" -o "$TMPFILE" "$SRC_LIB"

say "Sending ${DEST_NAME}..."
cp "$TMPFILE" "${DEST_DIR}${DEST_NAME}"

say "Done."

