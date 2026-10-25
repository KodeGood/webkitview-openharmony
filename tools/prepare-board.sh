#!/usr/bin/env bash
set -euo pipefail

# prepare-board.sh
# Enable native child process creation for apps by adding them to the whitelist JSON on the
# device, and bump const.max_native_child_process in /etc/param/appfwk.para to 40.
#
# Usage:
#   ./prepare-board.sh [app_identifier...]
# Default app identifier: 12341234 (webkitview). Apps are identified by the app-identifier of
# their signing profile (APP_IDENTIFIER of gen-signing-config.sh), e.g. 12341235 for Qt
# WebKitView. The identifiers are added to the ones already on the device, so apps whitelisted
# earlier stay allowed.

APP_IDS=("$@")
[[ ${#APP_IDS[@]} -gt 0 ]] || APP_IDS=(12341234)

die(){ echo "ERROR: $*" >&2; exit 1; }
need(){ command -v "$1" >/dev/null 2>&1 || die "Missing required tool: $1"; }

need hdc
need python3

# Resolve paths relative to project root (<project-root>/tools/prepare-board.sh)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(realpath "$SCRIPT_DIR/..")"

MEMMGR_CONFIG="$PROJECT_ROOT/prebuilts/memmgr_config.xml"
ALLOW_JSON="ability_runtime/allow_native_child_process_apps.json"

echo "==> Target app identifiers: ${APP_IDS[*]}"

# --- Temp files ---
TMP_JSON="$(mktemp)"
TMP_PARA="$(mktemp)"
TMP_EXISTING="$(mktemp -d)"
trap 'rm -rf "$TMP_JSON" "$TMP_PARA" "$TMP_EXISTING"' EXIT

echo "==> Fetching the current whitelist (if any)..."
hdc file recv "/system/etc/$ALLOW_JSON" "$TMP_EXISTING/system.json" >/dev/null 2>&1 || true
hdc file recv "/etc/$ALLOW_JSON" "$TMP_EXISTING/etc.json" >/dev/null 2>&1 || true

# Merge: existing identifiers first, in their order, then the new ones.
python3 -I - "$TMP_JSON" "$TMP_EXISTING/system.json" "$TMP_EXISTING/etc.json" -- "${APP_IDS[@]}" <<'PY'
import json, os, sys
args = sys.argv[1:]
sep = args.index("--")
out, existing, wanted = args[0], args[1:sep], args[sep + 1:]
ids = []
for path in existing:
    if not os.path.isfile(path) or os.path.getsize(path) == 0:
        continue
    try:
        apps = json.load(open(path)).get("allowNativeChildProcessApps", [])
    except ValueError:
        print(f"WARNING: ignoring unparsable {os.path.basename(path)} whitelist", file=sys.stderr)
        continue
    ids += [a["identifier"] for a in apps if isinstance(a, dict) and a.get("identifier")]
ids += wanted
ids = list(dict.fromkeys(ids))
with open(out, "w") as f:
    json.dump({"allowNativeChildProcessApps": [{"identifier": i} for i in ids]}, f, indent=2)
    f.write("\n")
print("==> Whitelisted app identifiers: " + " ".join(ids))
PY

echo "==> Checking device connectivity..."
hdc list targets || true

echo "==> Remounting / and /system as read-write (best effort)..."
hdc shell "mount -o remount,rw / || true"
hdc shell "mount -o remount,rw /system || true"

echo "==> Creating ability_runtime directories..."
hdc shell "mkdir -p /system/etc/ability_runtime"
hdc shell "mkdir -p /etc/ability_runtime || true"

echo "==> Sending JSON to /system/etc/$ALLOW_JSON..."
hdc file send "$TMP_JSON" "/system/etc/$ALLOW_JSON"
hdc shell "chmod 0644 /system/etc/$ALLOW_JSON"

echo "==> Sending JSON to /etc/$ALLOW_JSON (best effort)..."
hdc file send "$TMP_JSON" "/etc/$ALLOW_JSON" || true
hdc shell "chmod 0644 /etc/$ALLOW_JSON" || true

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
# echo "==> Sending $NATIVE_BUFFER_LIB -> /system/lib64/ndk/libnative_buffer_socket.z.so"
# hdc file send "$NATIVE_BUFFER_LIB" "/system/lib64/ndk/libnative_buffer_socket.z.so"
# hdc shell "chmod 0644 /system/lib64/ndk/libnative_buffer_socket.z.so"

echo "==> Sending $MEMMGR_CONFIG -> /system/etc/memmgr/memmgr_config.xml"
hdc file send "$MEMMGR_CONFIG" "/system/etc/memmgr/memmgr_config.xml"
hdc shell "chmod 0644 /system/etc/memmgr/memmgr_config.xml"

echo "==> Syncing filesystem..."
hdc shell sync

echo "==> Rebooting the device..."
hdc shell reboot

echo "==> Done. Device is rebooting."

