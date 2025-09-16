#!/usr/bin/env bash
set -euo pipefail

# prepare-board.sh
# Enable native child process creation for an app by writing the JSON directly on the device,
# and bump const.max_native_child_process in /etc/param/appfwk.para to 40.
#
# Usage:
#   ./prepare-board.sh [bundle_identifier]
# Default app identifier: 12341234

APP_ID="${1:-12341234}"

die(){ echo "ERROR: $*" >&2; exit 1; }
need(){ command -v "$1" >/dev/null 2>&1 || die "Missing required tool: $1"; }

need hdc

# Resolve paths relative to project root (<project-root>/tools/prepare-board.sh)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(realpath "$SCRIPT_DIR/..")"

NATIVE_BUFFER_LIB="$PROJECT_ROOT/prebuilts/libnative_buffer_socket.z.so"
MEMMGR_CONFIG="$PROJECT_ROOT/prebuilts/memmgr_config.xml"

echo "==> Target app identifier: ${APP_ID}"

# Build JSON content
JSON_CONTENT=$(cat <<EOF
{
  "allowNativeChildProcessApps": [
    { "identifier": "${APP_ID}" }
  ]
}
EOF
)

# --- Temp files ---
TMP_JSON="$(mktemp)"
TMP_PARA="$(mktemp)"
trap 'rm -f "$TMP_JSON" "$TMP_PARA"' EXIT
printf '%s\n' "$JSON_CONTENT" > "$TMP_JSON"

echo "==> Checking device connectivity..."
hdc list targets || true

echo "==> Remounting / and /system as read-write (best effort)..."
hdc shell "mount -o remount,rw / || true"
hdc shell "mount -o remount,rw /system || true"

echo "==> Creating ability_runtime directories..."
hdc shell "mkdir -p /system/etc/ability_runtime"
hdc shell "mkdir -p /etc/ability_runtime || true"

echo "==> Sending JSON to /system/etc/ability_runtime/allow_native_child_process_apps.json..."
hdc file send "$TMP_JSON" "/system/etc/ability_runtime/allow_native_child_process_apps.json"
hdc shell "chmod 0644 /system/etc/ability_runtime/allow_native_child_process_apps.json"

echo "==> Sending JSON to /etc/ability_runtime/allow_native_child_process_apps.json (best effort)..."
hdc file send "$TMP_JSON" "/etc/ability_runtime/allow_native_child_process_apps.json" || true
hdc shell "chmod 0644 /etc/ability_runtime/allow_native_child_process_apps.json" || true

echo "==> Fetching /etc/param/appfwk.para..."
if hdc file recv "/etc/param/appfwk.para" "$TMP_PARA"; then
  echo "==> Patching const.max_native_child_process to 40..."
  if grep -q '^const.max_native_child_process' "$TMP_PARA"; then
    sed -i 's/^const.max_native_child_process *= *.*/const.max_native_child_process = 40/' "$TMP_PARA"
  else
    echo "const.max_native_child_process = 40" >> "$TMP_PARA"
  fi

  echo "==> Uploading patched appfwk.para..."
  hdc file send "$TMP_PARA" "/etc/param/appfwk.para"
  hdc shell "chmod 0644 /etc/param/appfwk.para"
else
  echo "WARNING: Could not fetch /etc/param/appfwk.para, skipping patch."
fi

# --- Push prebuilts ---
echo "==> Sending $NATIVE_BUFFER_LIB -> /system/lib64/ndk/libnative_buffer_socket.z.so"
hdc file send "$NATIVE_BUFFER_LIB" "/system/lib64/ndk/libnative_buffer_socket.z.so"
hdc shell "chmod 0644 /system/lib64/ndk/libnative_buffer_socket.z.so"

echo "==> Sending $MEMMGR_CONFIG -> /system/etc/memmgr/memmgr_config.xml"
hdc file send "$MEMMGR_CONFIG" "/system/etc/memmgr/memmgr_config.xml"
hdc shell "chmod 0644 /system/etc/memmgr/memmgr_config.xml"

echo "==> Syncing filesystem..."
hdc shell sync

echo "==> Rebooting the device..."
hdc shell reboot

echo "==> Done. Device is rebooting."

