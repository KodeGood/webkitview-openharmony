#!/usr/bin/env bash
# Self-contained signing: local PKCS#12 keystore.p12 (one strong password),
# autosign-style local CAs, app cert issued by SDK OpenHarmony Application CA,
# profile signed by SDK profile key, UDIDs + AllowAppMultiProcess,
# and bundle-info.app-identifier support.

# ================= helpers =================
die(){ echo "ERROR: $*" >&2; exit 2; }
say(){ printf "\n==> %s\n" "$*"; }
need(){ command -v "$1" >/dev/null 2>&1 || die "Missing '$1' in PATH"; }
q(){ printf "'%s' " "$@"; }  # quote-echo

randpwd () {
  # 48+ chars, URL/FS-friendly; robust on GNU/BSD/macOS (note '-' at end)
  local filter='A-Za-z0-9._@#%+=-'
  if command -v openssl >/dev/null 2>&1; then
    LC_ALL=C openssl rand -base64 64 | LC_ALL=C tr -dc "$filter" | head -c 48; echo
  else
    set +o pipefail
    LC_ALL=C dd if=/dev/urandom bs=64 count=1 2>/dev/null \
      | base64 | LC_ALL=C tr -dc "$filter" | head -c 48; echo
    set -o pipefail
  fi
}

collect_udids () {
  local ids=""; local keys="";
  if command -v hdc >/dev/null 2>&1; then
    keys="$(hdc list targets 2>/dev/null \
      | awk '{print $1}' \
      | sed -E '/^(List|$)/d; /^\/dev\//d; /^(device|offline|unknown)$/d' \
      | sed -E '/^target(s)?:/Id' \
      | sed '/^[[:space:]]*$/d' | sort -u)"
    if [[ -n "$keys" ]]; then
      while IFS= read -r k; do
        [[ -n "$k" ]] || continue
        local u; u="$(hdc -t "$k" shell bm get --udid 2>/dev/null | tr -d '\r' | tail -n1 | xargs || true)"
        [[ -n "$u" && "$u" != "null" && "$u" != "unknown" ]] && ids+="$u"$'\n'
      done <<< "$keys"
    else
      local u; u="$(hdc shell bm get --udid 2>/dev/null | tr -d '\r' | tail -n1 | xargs || true)"
      [[ -n "$u" && "$u" != "null" && "$u" != "unknown" ]] && ids="$u"
    fi
  fi
  [[ -n "${UDIDS:-}" ]] && ids="$ids"$'\n'"$(echo "$UDIDS" | tr ',' '\n')"
  echo "$ids" | sed '/^$/d' | sort -u
}

# ================= args & defaults =================
DEFAULT_BUNDLE_NAME="com.kodegood.webkitview"
DEFAULT_OUTDIR=".autosign"
PROFILE_KIND="${PROFILE_KIND:-debug}"  # debug | release
APP_IDENTIFIER="${APP_IDENTIFIER:-12341234}"  # <— NEW: default app-identifier

if [[ $# -ge 1 ]]; then BUNDLE_NAME="$1"; else BUNDLE_NAME="${BUNDLE_NAME:-$DEFAULT_BUNDLE_NAME}"; fi
if [[ $# -ge 2 ]]; then OUTDIR="$2"; else OUTDIR="${OUTDIR:-$DEFAULT_OUTDIR}"; fi
if [[ $# -ge 3 ]]; then OHOS_SDK_TOOLCHAINS_EFFECTIVE="$3"; else OHOS_SDK_TOOLCHAINS_EFFECTIVE="${OHOS_SDK_TOOLCHAINS:-}"; fi

if [[ -z "${OHOS_SDK_TOOLCHAINS_EFFECTIVE:-}" ]]; then
  cat <<USAGE
Usage: $0 [BUNDLE_NAME [OUTDIR [OHOS_SDK_TOOLCHAINS]]]

OHOS_SDK_TOOLCHAINS = /path/to/ohos-sdk/<ver>/toolchains  (arg #3 or env)
Defaults:
  BUNDLE_NAME = '$DEFAULT_BUNDLE_NAME'
  OUTDIR      = '$DEFAULT_OUTDIR'

Env toggles:
  PROFILE_KIND=debug|release        (default: debug)
  SUBJECT_DN_APP='C=...,O=...,OU=...,CN=...'
  APP_IDENTIFIER='12341234'         (bundle-info.app-identifier, default shown)
USAGE
  exit 1
fi

SDK_LIB="$OHOS_SDK_TOOLCHAINS_EFFECTIVE/lib"

say "Using:"
echo "  BUNDLE_NAME         = $BUNDLE_NAME"
echo "  OUTDIR              = $OUTDIR"
echo "  OHOS_SDK_TOOLCHAINS = $OHOS_SDK_TOOLCHAINS_EFFECTIVE"
echo "  PROFILE_KIND        = $PROFILE_KIND"
echo "  APP_IDENTIFIER      = $APP_IDENTIFIER"

need keytool
need java
HAVE_JQ=1; command -v jq >/dev/null 2>&1 || HAVE_JQ=0

# ======== TEMP REQUIREMENT: UDIDs must exist, or abort ========
# Collect once and fail early to avoid generating any keys/files.
RAW_UDIDS="$(collect_udids || true)"

# Filter only valid UDIDs (hex strings with optional dashes, at least 16 chars)
VALID_UDIDS="$(echo "$RAW_UDIDS" | grep -E '^[A-Fa-f0-9-]{16,}$' || true)"
if [[ -z "$VALID_UDIDS" ]]; then
  die "No valid UDIDs found (invalid or device not connected). Connect device or set UDIDS=... manually."
fi

# ============== locate SDK files (read-only) ===============
P12="${SDK_LIB}/OpenHarmony.p12"
PROFILE_PEM="$SDK_LIB/OpenHarmonyProfile${PROFILE_KIND^}.pem"
PROFILE_TMPL="${SDK_LIB}/Unsgned${PROFILE_KIND^}ProfileTemplate.json"  # sometimes "Unsigned"
[[ -f "$PROFILE_TMPL" ]] || PROFILE_TMPL="${SDK_LIB}/Unsigned${PROFILE_KIND^}ProfileTemplate.json"
HAP_JAR="${SDK_LIB}/hap-sign-tool.jar"
for f in "$P12" "$PROFILE_PEM" "$PROFILE_TMPL" "$HAP_JAR"; do
  [[ -f "$f" ]] || die "Missing: $f"
done

# ================= strict + trap ===========================
set -Eeuo pipefail
trap 'rc=$?; echo; echo "!! ERROR at line $LINENO: $BASH_COMMAND (exit $rc)"; exit $rc' ERR

# ============ passwords (ONE strong password) ==============
KS_PWD="${KS_PWD:-$(randpwd)}"     # single strong password for local keystore.p12 AND key entry
[[ ${#KS_PWD} -ge 32 ]] || die "KS_PWD must be >= 32 characters"
KEY_PWD="$KS_PWD"  # PKCS#12: keyPass == storePass

# App certificate subject (autosign-style by default)
SUBJECT_DN_APP="${SUBJECT_DN_APP:-C=CN,O=OpenHarmony,OU=OpenHarmony Team,CN=OpenHarmony Application Release}"

ALIAS="devkey-${BUNDLE_NAME//./-}-$(date +%s)"
SDK_STOREPASS="123456"

# ============== prepare OUTDIR =============================
mkdir -p "$OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"
say "Output directory: $OUTDIR"
cp -f "$HAP_JAR" "$OUTDIR/hap-sign-tool.jar"
cd "$OUTDIR"

# ============ discover SDK aliases ========================
ALIASES=$(keytool -list -v -storetype PKCS12 -keystore "$P12" -storepass "$SDK_STOREPASS" \
  | awk -F': ' '/^Alias name:/{sub(/^[[:space:]]+/,"",$2); print $2}')
SUB_ALIAS=$(printf '%s\n' "$ALIASES" | awk 'BEGIN{IGNORECASE=1} /application/ && /ca/ && !/profile/ {print; exit}')
ROOT_ALIAS=$(printf '%s\n' "$ALIASES" | awk 'BEGIN{IGNORECASE=1} /root/ && /ca/ {print; exit}')
PROFILE_ALIAS=$(printf '%s\n' "$ALIASES" | awk -v kind="$PROFILE_KIND" 'BEGIN{IGNORECASE=1} /application/ && /profile/ && (kind=="debug"?/debug/:/release/) {print; exit}')
: "${SUB_ALIAS:=openharmony application ca}"
: "${ROOT_ALIAS:=openharmony application root ca}"
: "${PROFILE_ALIAS:=openharmony application profile '"$PROFILE_KIND"'}"

[[ -n "$SUB_ALIAS" && -n "$ROOT_ALIAS" && -n "$PROFILE_ALIAS" ]] || die "Missing expected aliases in OpenHarmony.p12"

# ============ export SDK CA certs ==========================
say "Exporting SDK CA certs"
keytool -exportcert -rfc -alias "$SUB_ALIAS"  -keystore "$P12" -storetype PKCS12 -storepass "$SDK_STOREPASS" -file subCA.cer
keytool -exportcert -rfc -alias "$ROOT_ALIAS" -keystore "$P12" -storetype PKCS12 -storepass "$SDK_STOREPASS" -file rootCA.cer

# ============== AUTOSIGN-LIKE LOCAL CA HIERARCHY ==========
say "Creating local PKCS#12 keystore (keystore.p12, one strong password) and autosign-style CAs"
java -jar "hap-sign-tool.jar" generate-keypair \
  -keystoreFile "keystore.p12" -keystorePwd "$KS_PWD" \
  -keyAlias oh-profile-key-v1 -keyPwd "$KS_PWD" \
  -keyAlg ECC -keySize NIST-P-256

java -jar "hap-sign-tool.jar" generate-ca \
  -keystoreFile keystore.p12 -keystorePwd "$KS_PWD" \
  -keyAlias oh-root-ca-key-v1 -keyPwd "$KS_PWD" \
  -subject "C=CN,O=OpenHarmony,OU=OpenHarmony Community,CN=Root CA" \
  -signAlg "SHA256withECDSA" -validity "365" \
  -keyAlg ECC -keySize NIST-P-256 \
  -outFile "root-ca1.cer" \
  -issuerKeyPwd "$KS_PWD"

java -jar "hap-sign-tool.jar" generate-ca \
  -keystoreFile keystore.p12 -keystorePwd "$KS_PWD" \
  -keyAlias oh-app-sign-srv-ca-key-v1 -keyPwd "$KS_PWD" \
  -subject "C=CN,O=OpenHarmony,OU=OpenHarmony Community,CN= Application Signature Service CA" \
  -issuer "C=CN,O=OpenHarmony,OU=OpenHarmony Community,CN=Root CA" \
  -issuerKeyAlias oh-root-ca-key-v1 -issuerKeyPwd "$KS_PWD" \
  -signAlg "SHA256withECDSA" -validity "365" \
  -keyAlg ECC -keySize NIST-P-256 \
  -outFile "app-sign-srv-ca1.cer"

java -jar "hap-sign-tool.jar" generate-ca \
  -keystoreFile keystore.p12 -keystorePwd "$KS_PWD" \
  -keyAlias oh-profile-sign-srv-ca-key-v1 -keyPwd "$KS_PWD" \
  -subject "C=CN,O=OpenHarmony,OU=OpenHarmony Community,CN= Profile Signature Service CA" \
  -issuer "C=CN,O=OpenHarmony,OU=OpenHarmony Community,CN=Root CA" \
  -issuerKeyAlias oh-root-ca-key-v1 -issuerKeyPwd "$KS_PWD" \
  -signAlg "SHA256withECDSA" -validity "365" \
  -keyAlg ECC -keySize NIST-P-256 \
  -outFile "profile-sign-srv-ca1.cer"

java -jar "hap-sign-tool.jar" generate-profile-cert \
  -keystoreFile keystore.p12 -keystorePwd "$KS_PWD" \
  -keyAlias oh-profile-key-v1 -keyPwd "$KS_PWD" \
  -subject "C=CN,O=OpenHarmony,OU=OpenHarmony Community,CN=Profile1 ${PROFILE_KIND^}" \
  -issuer "C=CN,O=OpenHarmony,OU=OpenHarmony Community,CN= Profile Signature Service CA" \
  -issuerKeyAlias oh-profile-sign-srv-ca-key-v1 -issuerKeyPwd "$KS_PWD" \
  -subCaCertFile profile-sign-srv-ca1.cer \
  -rootCaCertFile root-ca1.cer \
  -signAlg "SHA256withECDSA" -validity "365" \
  -outForm "certChain" \
  -outFile "local_profile_cert_chain.pem"

# ============== APP KEYPAIR + CERT (SDK CA) ===============
say "Generating app keypair in OpenHarmony.p12 and issuing cert from SDK CA"
# IMPORTANT: key password MUST equal SDK store password for PKCS#12 source reads
java -jar "hap-sign-tool.jar" generate-keypair \
  -keystoreFile "$P12" -keystorePwd "$SDK_STOREPASS" \
  -keyAlias "$ALIAS" -keyPwd "$SDK_STOREPASS" \
  -keyAlg ECC -keySize NIST-P-256
keytool -list -v -storetype PKCS12 -keystore "$P12" -storepass "$SDK_STOREPASS" \
  | grep -F "Alias name: $ALIAS" >/dev/null || die "App alias missing in OpenHarmony.p12"

java -jar "hap-sign-tool.jar" generate-app-cert \
  -keystoreFile "$P12" -keystorePwd "$SDK_STOREPASS" \
  -keyAlias "$ALIAS" -keyPwd "$SDK_STOREPASS" \
  -issuer "C=CN,O=OpenHarmony,OU=OpenHarmony Team,CN=OpenHarmony Application CA" \
  -issuerKeyAlias "$SUB_ALIAS" -issuerKeyPwd "$SDK_STOREPASS" \
  -subject "$SUBJECT_DN_APP" \
  -subCaCertFile "subCA.cer" -rootCaCertFile "rootCA.cer" \
  -signAlg "SHA256withECDSA" -validity "365" \
  -outForm "certChain" -outFile "app_cert_chain.pem"

# app leaf (for development-certificate in profile)
awk 'f==0 && /-----BEGIN CERTIFICATE-----/{f=1} f{print} /-----END CERTIFICATE-----/{exit}' app_cert_chain.pem > app_entity.cer

# Import the app private key from SDK p12 into our local PKCS#12 (one strong password)
say "Importing app private key into keystore.p12 and attaching issued chain"
keytool -importkeystore -noprompt \
  -srckeystore "$P12" -srcstoretype PKCS12 -srcstorepass "$SDK_STOREPASS" \
  -srcalias "$ALIAS" -srckeypass "$SDK_STOREPASS" \
  -destkeystore "keystore.p12" -deststoretype PKCS12 \
  -deststorepass "$KS_PWD" -destalias "$ALIAS" -destkeypass "$KS_PWD"

keytool -importcert -noprompt \
  -alias "$ALIAS" \
  -file "app_cert_chain.pem" \
  -keystore "keystore.p12" -storetype PKCS12 -storepass "$KS_PWD"

# =============== Build profile JSON =======================
say "Preparing ${PROFILE_KIND} profile (bundle + UDIDs + development-certificate + AllowAppMultiProcess + app-identifier)"
NOW=$(date +%s); NOT_AFTER=$(( NOW + 315360000 )) # ~10 years
[[ -n "$RAW_UDIDS" ]] && say "UDIDs found: $(printf '%s' "$RAW_UDIDS" | wc -l | tr -d ' ') device(s)" || say "No UDIDs via hdc; set UDIDS env or connect a device."

if [[ $HAVE_JQ -eq 1 ]]; then
  if [[ -n "$RAW_UDIDS" ]]; then mapfile -t arr < <(printf '%s' "$RAW_UDIDS"); else arr=(); fi
  jq --arg bn "$BUNDLE_NAME" --argjson nb "$NOW" --argjson na "$NOT_AFTER" \
     --argjson dids "$(printf '%s\n' "${arr[@]}" | jq -R . | jq -s .)" \
     --rawfile devpem "app_entity.cer" \
     --arg appid "$APP_IDENTIFIER" '
    .type = "'"$PROFILE_KIND"'"
    | .["version-name"]="1.0.0"
    | .validity["not-before"] = $nb
    | .validity["not-after"]  = $na
    | .["bundle-info"]["bundle-name"] = $bn
    | .["bundle-info"].bundleName      = $bn
    | .["bundle-info"]["app-identifier"] = $appid
    | .["bundle-info"]["development-certificate"] = $devpem
    | .["debug-info"]["device-id-type"] = "udid"
    | .["debug-info"]["device-ids"] = ( $dids // [] )
    | .["app-privilege-capabilities"] = ["AllowAppMultiProcess"]
  ' "$PROFILE_TMPL" > profile_unsigned.json
else
  cp "$PROFILE_TMPL" profile_unsigned.json
  # GNU sed; on macOS you may need: sed -i '' -E ...
  sed -i -E \
    -e "s/(\"bundle-name\" *: *\").*(\")/\1$BUNDLE_NAME\2/" \
    -e "s/(\"bundleName\" *: *\").*(\")/\1$BUNDLE_NAME\2/" \
    -e "s/(\"not-before\" *: *)[0-9]+/\1$NOW/" \
    -e "s/(\"not-after\" *: *)[0-9]+/\1$NOT_AFTER/" \
    -e "s/(\"device-id-type\" *: *\").*(\")/\1udid\2/" \
    -e "s/(\"type\" *: *\").*(\")/\1$PROFILE_KIND\2/" \
    profile_unsigned.json || true

  # development-certificate (JSON-escaped)
  if command -v python3 >/dev/null 2>&1; then
    DEV_CERT_JSONSTR="$(python3 - <<'PY' < app_entity.cer
import sys, json; print(json.dumps(sys.stdin.read()))
PY
)"; else die "Install jq or python3 to embed development-certificate cleanly."; fi
  perl -0777 -pe "s/\"development-certificate\"\\s*:\\s*\"(?:[^\"\\\\]|\\\\.)*\"/\"development-certificate\": ${DEV_CERT_JSONSTR}/s" -i profile_unsigned.json

  # device-ids
  if [[ -n "$RAW_UDIDS" ]]; then
    ids_joined="$(printf '%s' "$RAW_UDIDS" | awk '{printf "%s\"%s\"", (NR>1?", ":""), $0}')"
    perl -0777 -pe "s/\"device-ids\"\\s*:\\s*\\[[^\\]]*\\]/\"device-ids\": [ ${ids_joined} ]/s" -i profile_unsigned.json
  fi

  # app-privilege-capabilities (if missing)
  if ! grep -q '"app-privilege-capabilities"' profile_unsigned.json; then
    perl -0777 -pe 's/"issuer"\s*:\s*/"app-privilege-capabilities": ["AllowAppMultiProcess"],\n  "issuer": /' -i profile_unsigned.json
  fi

  # app-identifier (robustly set using python to avoid JSON regex pitfalls)
  python3 - "$APP_IDENTIFIER" <<'PY'
import json, sys, os
fn = "profile_unsigned.json"
appid = sys.argv[1]
with open(fn, "r", encoding="utf-8") as f:
    d = json.load(f)
d.setdefault("bundle-info", {})["app-identifier"] = appid
with open(fn, "w", encoding="utf-8") as f:
    json.dump(d, f, ensure_ascii=False, indent=2)
PY
fi

# =============== Sign profile with SDK key =================
say "Signing ${PROFILE_KIND} profile with OpenHarmony.p12 + OpenHarmonyProfile${PROFILE_KIND^}.pem"
java -jar "hap-sign-tool.jar" sign-profile \
  -mode "localSign" -signAlg "SHA256withECDSA" \
  -keystoreFile "$P12" -keystorePwd "$SDK_STOREPASS" \
  -keyAlias "$PROFILE_ALIAS" -keyPwd "$SDK_STOREPASS" \
  -profileCertFile "$PROFILE_PEM" \
  -inFile "profile_unsigned.json" -outFile "profile.p7b"

# ============== Fingerprint & config =======================
say "Computing SHA256 fingerprint (app leaf)"
FPR=$(keytool -printcert -file app_entity.cer \
  | tr '[:lower:]' '[:upper:]' \
  | grep -Eo '[A-F0-9]{2}(:[A-F0-9]{2}){31}|[A-F0-9]{64}' \
  | head -n1 | tr -d ':')
echo "$FPR" > APP_CERT_SHA256.txt

say "Writing signing-configs.json (uses local keystore.p12, one strong password)"
cat > signing-configs.json <<JSON
[
  {
    "name": "defaultDebugCustom",
    "type": "OpenHarmony",
    "material": {
      "storeFile": "${OUTDIR}/keystore.p12",
      "storePassword": "${KS_PWD}",
      "keyAlias": "${ALIAS}",
      "keyPassword": "${KS_PWD}",
      "certpath": "${OUTDIR}/app_cert_chain.pem",
      "profile": "${OUTDIR}/profile.p7b",
      "signAlg": "SHA256withECDSA"
    }
  }
]
JSON

# ============== Summary ====================================
say "Done. Outputs:"
ls -l "${OUTDIR}"/{keystore.p12,root-ca1.cer,app-sign-srv-ca1.cer,profile-sign-srv-ca1.cer,local_profile_cert_chain.pem,subCA.cer,rootCA.cer,app_cert_chain.pem,app_entity.cer,profile_unsigned.json,profile.p7b,signing-configs.json,APP_CERT_SHA256.txt,hap-sign-tool.jar} 2>/dev/null || true

echo
echo "In build-profile.json5 set:"
echo '  "products": [{ "name": "default", "signingConfig": "defaultDebugCustom" }]'
echo
echo "Notes:"
echo " - Local keystore.p12 uses ONE strong password (KS_PWD), as PKCS#12 expects."
echo " - App key in SDK p12 uses keyPwd=123456 (same as SDK store) so import works."
echo " - App cert is issued by SDK OpenHarmony Application CA (subCA/rootCA extracted)."
echo " - Profile is signed by SDK profile key (OpenHarmony.p12 + OpenHarmonyProfile${PROFILE_KIND^}.pem)."
echo " - Profile embeds app leaf as development-certificate, UDIDs, AllowAppMultiProcess, and app-identifier=${APP_IDENTIFIER}."
echo " - Override app cert subject with SUBJECT_DN_APP; override app-identifier with APP_IDENTIFIER."

