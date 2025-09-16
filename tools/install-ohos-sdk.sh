#!/usr/bin/env bash
set -euo pipefail

# OpenHarmony SDK installer from tar.gz (platform-aware, no jq required)
# Layout inside tar.gz:
#   **/<platform>/*.zip          where <platform> ∈ {linux, windows, darwin}
# Each ZIP contains oh-uni-package.json with:
#   { "apiVersion": "20", "path": "native" | "toolchains" | "ets" | "js" | "previewer", ... }
#
# Installs to:
#   <target_dir>/<apiVersion>/<path>/
#
# Example:
#   ./install-ohos-sdk.sh /path/SDK-bundle.tar.gz linux /opt/ohos-sdk --force

die(){ echo "ERROR: $*" >&2; exit 2; }
say(){ printf "\n==> %s\n" "$*"; }
need(){ command -v "$1" >/dev/null 2>&1 || die "Missing required command: $1"; }

usage(){
  cat <<'EOF'
Usage:
  install-ohos-sdk.sh <sdk_tar_gz> <platform> <target_dir> [--force]

Args:
  sdk_tar_gz  : Path to the SDK .tar.gz archive
  platform    : One of: linux | windows | darwin
  target_dir  : Installation root directory
  --force     : Overwrite existing component dirs if present

Notes:
- Does NOT require jq, but will use it if present.
- Reads apiVersion and path from each ZIP's oh-uni-package.json.
- Verifies all ZIPs share the same apiVersion.
- Avoids double-nesting if a ZIP already contains a top-level <path>/ dir.
EOF
}

[[ $# -lt 3 ]] && { usage; exit 2; }
SDK_TGZ="$1"; PLATFORM="$2"; TARGET_DIR="$3"; FORCE="${4-}"

[[ -f "$SDK_TGZ" ]] || die "File not found: $SDK_TGZ"
case "$PLATFORM" in linux|windows|darwin) ;; *) die "platform must be one of: linux | windows | darwin";; esac
[[ -z "${FORCE:-}" || "$FORCE" == "--force" ]] || die "Unknown flag: $FORCE"

# Required tools
need tar
need unzip
need mktemp
need awk
need sed
# jq is optional
HAVE_JQ=0; command -v jq >/dev/null 2>&1 && HAVE_JQ=1

# Resolve paths
if command -v realpath >/dev/null 2>&1; then
  SDK_TGZ="$(realpath "$SDK_TGZ")"
  TARGET_DIR="$(realpath -m "$TARGET_DIR")"
fi

WORKDIR="$(mktemp -d -t ohos-sdk-XXXXXX)"
cleanup(){ rm -rf "$WORKDIR" 2>/dev/null || true; }
trap cleanup EXIT

say "Extracting archive to temp workspace"
tar -xzf "$SDK_TGZ" -C "$WORKDIR"

say "Looking for ZIPs under **/${PLATFORM}/*.zip"
mapfile -t ZIPS < <(find "$WORKDIR" -type f -path "*/${PLATFORM}/*.zip" | sort)
((${#ZIPS[@]} > 0)) || die "No ZIPs found for platform '$PLATFORM'."

say "Found ${#ZIPS[@]} ZIP(s)"

# Helpers
pick_oh_uni_json(){
  local zip="$1"
  unzip -Z1 "$zip" | awk 'tolower($0) ~ /(^|\/)oh-uni-package\.json$/ {print; exit}'
}

read_json_field(){
  # $1=key, JSON on stdin. Uses jq if available; else minimal grep/sed.
  local key="$1"
  if ((HAVE_JQ)); then
    jq -r --arg k "$key" '.[$k] // empty'
  else
    # Extract "key": "value" or "key": value
    grep -Eo "\"${key}\"[[:space:]]*:[[:space:]]*\"[^\"]*\"|\"${key}\"[[:space:]]*:[[:space:]]*[0-9.]+" \
      | head -n1 \
      | sed -E 's/^[^:]*:[[:space:]]*"?(.*?)"?$/\1/'
  fi
}

API_VERSION=""
declare -A INSTALLED=()

for zip in "${ZIPS[@]}"; do
  say "Processing $zip"
  entry="$(pick_oh_uni_json "$zip" || true)"
  [[ -n "$entry" ]] || die "oh-uni-package.json not found in: $zip"

  json="$(unzip -p "$zip" "$entry")"
  [[ -n "$json" ]] || die "Failed reading $entry from $zip"

  this_api="$(printf '%s' "$json" | read_json_field apiVersion | sed -E 's/^[[:space:]]+|[[:space:]]+$//g')"
  comp_path="$(printf '%s' "$json" | read_json_field path       | sed -E 's/^[[:space:]]+|[[:space:]]+$//g')"

  [[ -n "$this_api" ]]  || die "apiVersion missing in $zip"
  [[ -n "$comp_path" ]] || die "path missing in $zip"

  if [[ -z "$API_VERSION" ]]; then
    API_VERSION="$this_api"
    say "Detected API version: $API_VERSION"
  elif [[ "$API_VERSION" != "$this_api" ]]; then
    die "Mismatching apiVersion across ZIPs: '$API_VERSION' vs '$this_api' in $zip"
  fi

  dest="$TARGET_DIR/$API_VERSION/$comp_path"
  mkdir -p "$dest"

  tmp_out="$WORKDIR/extract_$(echo "$comp_path" | tr '/ ' '__')"
  rm -rf "$tmp_out"; mkdir -p "$tmp_out"

  unzip -q -o "$zip" -d "$tmp_out"

  # Avoid double-nesting if ZIP already contains top-level <path>/
  src="$tmp_out"
  [[ -d "$tmp_out/$comp_path" ]] && src="$tmp_out/$comp_path"

  if [[ -d "$dest" && "${FORCE:-}" == "--force" ]]; then
    rm -rf "${dest:?}/"*
  elif [[ -n "$(find "$dest" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]]; then
    die "Destination not empty: $dest (use --force to overwrite)"
  fi

  # Determine where the metadata file is so we can ensure it lands in $dest
  meta_in_src=""
  if [[ -f "$src/oh-uni-package.json" ]]; then
    meta_in_src="$src/oh-uni-package.json"
  elif [[ -f "$tmp_out/oh-uni-package.json" ]]; then
    # Handles case where the ZIP places oh-uni-package.json at the ZIP root
    meta_in_src="$tmp_out/oh-uni-package.json"
  fi

  # Copy component files (do NOT exclude metadata)
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete "$src"/ "$dest"/
  else
    cp -a "$src"/. "$dest"/
  fi

  # Ensure metadata exists in destination (overwrite with a clean copy)
  if [[ -n "$meta_in_src" ]]; then
  install -m 0644 "$meta_in_src" "$dest/oh-uni-package.json"
  fi

  INSTALLED["$comp_path"]=1
done

say "Installation complete."
echo "Installed into: $TARGET_DIR/$API_VERSION"
for comp in native toolchains ets js previewer; do
  [[ -n "${INSTALLED[$comp]+x}" ]] && echo "  - $comp"
done

