#!/bin/bash
# OpenHarmony FaultLogger symbolizer
#
# Usage:
#   symbolize_crash_log.sh <crash_log_file> [--tool llvm-addr2line|llvm-symbolizer] [lib_dir1 [lib_dir2 ...]]
#
# Env knobs:
#   SUMMARY_DEPTH=N   # default 5 (frames per thread in summary)
#   NO_COLOR=1        # disable ANSI colors
#   APP_LIB_REGEX='libwebkitview\.so|com\.kodegood\.webkitview'  # highlight as "app"
#   LIB_PATHS_FILE=/path/to/file  # override dotfile name (default: .symbolize-lib-paths in CWD)
#
# Requires: OHOS_SDK_NATIVE to point at your OHOS SDK root that contains llvm/bin.

set -euo pipefail

# ------------------ Args ------------------
if [ "$#" -lt 1 ]; then
  cat <<EOF
Usage: $0 <crash_log_file> [--tool llvm-addr2line|llvm-symbolizer] [lib_dir1 [lib_dir2 ...]]
Examples:
  $0 crash.log /path/unstripped1 /path/unstripped2
  $0 crash.log --tool llvm-symbolizer /path/unstripped
  LIB_PATHS_FILE=~/my-lib-paths.txt $0 crash.log
EOF
  exit 1
fi

CRASH_LOG="$1"; shift

TOOL="llvm-addr2line"   # default; you can pass --tool to change
declare -a LIB_DIRS=()

# parse optional --tool and any number of lib dirs
if [[ "${1:-}" == "--tool" && -n "${2:-}" ]]; then
  TOOL="$2"; shift 2
fi
# remaining args are lib dirs
while [[ $# -gt 0 ]]; do
  LIB_DIRS+=("$1"); shift
done

# If no lib dirs provided, try dotfile list before falling back to paths in log
LIB_PATHS_FILE="${LIB_PATHS_FILE:-.symbolize-lib-paths}"
if [[ ${#LIB_DIRS[@]} -eq 0 && -f "$LIB_PATHS_FILE" ]]; then
  while IFS= read -r p; do
    # strip comments/whitespace
    p="${p%%#*}"
    p="$(printf '%s' "$p" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
    [[ -z "$p" ]] && continue
    LIB_DIRS+=("$p")
  done < "$LIB_PATHS_FILE"
fi

# ------------------ Config ------------------
: "${OHOS_SDK_NATIVE:?Set OHOS_SDK_NATIVE to your OHOS SDK root (for llvm/bin)}"
LLVM_BIN="$OHOS_SDK_NATIVE/llvm/bin"
SYM="$LLVM_BIN/llvm-symbolizer"
ADDR2LINE="$LLVM_BIN/llvm-addr2line"
NM="$LLVM_BIN/llvm-nm"
READELF="$LLVM_BIN/llvm-readelf"

SUMMARY_DEPTH="${SUMMARY_DEPTH:-5}"
APP_LIB_REGEX="${APP_LIB_REGEX:-libwebkitview\.so|com\.kodegood\.webkitview}"

# ------------------ Colors ------------------
use_color=1
if [[ -n "${NO_COLOR:-}" || ! -t 1 ]]; then use_color=0; fi
c_reset(){ [[ $use_color -eq 1 ]] && printf '\033[0m'; }
c_dim(){   [[ $use_color -eq 1 ]] && printf '\033[2m'; }
c_red(){   [[ $use_color -eq 1 ]] && printf '\033[31m'; }
c_yel(){   [[ $use_color -eq 1 ]] && printf '\033[33m'; }
c_blu(){   [[ $use_color -eq 1 ]] && printf '\033[34m'; }
c_mag(){   [[ $use_color -eq 1 ]] && printf '\033[35m'; }
c_cyn(){   [[ $use_color -eq 1 ]] && printf '\033[36m'; }

paint_line() {
  local line="$1" colored="$1"
  if [[ "$line" =~ Fault\ thread\ info: ]]; then
    colored="$(c_red)${line}$(c_reset)"
  elif [[ "$line" =~ \#([0-9]+)\ pc\  ]]; then
    if [[ "$line" =~ \#([0-9]+)\ pc\ [0-9a-fA-Fx]+\ (/[^[:space:]]+) ]]; then
      local lib="${BASH_REMATCH[2]}"
      local base="$(basename "${lib%%(*}")"
      if [[ "$base" =~ $APP_LIB_REGEX ]]; then
        colored="$(c_yel)${line}$(c_reset)"
      elif [[ "$base" =~ ^libWPE(WebKit|.+)\.so$ || "$base" == libWPEWebKit-2.0.so ]]; then
        colored="$(c_cyn)${line}$(c_reset)"
      elif [[ "$base" =~ ^libg(object|lib|io|thread|module|regex|dbus|) || "$base" == libglib-2.0.so || "$base" == libgobject-2.0.so ]]; then
        colored="$(c_mag)${line}$(c_reset)"
      elif [[ "$lib" == /system/* || "$lib" == /vendor/* ]]; then
        colored="$(c_dim)${line}$(c_reset)"
      fi
    fi
  fi
  printf '%b\n' "$colored"
}

# ------------------ Helpers ------------------
normalize_hex() {
  local a="$1"
  if [[ "$a" =~ ^0x[0-9a-fA-F]+$ ]]; then printf '%s' "$a"; else
    a="${a##0}"; [[ -z "$a" ]] && a="0"; printf '0x%x' $((16#$a))
  fi
}

symbolize_pc() {
  local obj="$1" addr="$2"
  local A; A="$(normalize_hex "$addr")"
  if [[ "$TOOL" == "llvm-symbolizer" ]]; then
    "$SYM" --demangle --pretty-print --inlines --obj="$obj" "$A" 2>/dev/null || true
  else
    "$ADDR2LINE" -e "$obj" -f -C -p "$A" || true
  fi
}

resolve_via_funcoff() {
  local obj="$1" funcoff="$2"  # e.g. webkit_network_session_new+108
  local func="${funcoff%%+*}"
  local off_str="${funcoff#*+}"
  [[ -z "$func" || -z "$off_str" ]] && return 1
  local off
  if [[ "$off_str" =~ ^0x[0-9a-fA-F]+$ ]]; then off=$((off_str)); else off=$((10#$off_str)); fi
  local sym_hex
  sym_hex="$("$NM" -C --defined-only "$obj" | awk -v f="$func" '$3==f {print $1; exit}')"
  [[ -z "$sym_hex" ]] && sym_hex="$("$NM" -C --defined-only "$obj" | awk -v f="$func" 'index($3,f)>0 {print $1; exit}')"
  [[ -z "$sym_hex" ]] && return 1
  local sym=$((16#$sym_hex))
  local abs=$((sym + off))
  symbolize_pc "$obj" "0x$(printf '%x' "$abs")"
}

# prefer any of the lib dirs; otherwise (if no lib dirs at all) allow the path in the log
choose_obj() {
  local path_in_log="$1"
  local base="$(basename "${path_in_log%%(*}")"
  local cand

  # 1) exact in provided lib dirs (search order preserved)
  for d in "${LIB_DIRS[@]}"; do
    cand="$d/$base"
    [[ -f "$cand" ]] && { printf '%s' "$cand"; return; }
  done

  # 2) if we *had* no lib dirs (user provided none and dotfile empty/missing), allow the log path
  if [[ ${#LIB_DIRS[@]} -eq 0 ]]; then
    path_in_log="${path_in_log%%(*}"
    [[ -f "$path_in_log" ]] && { printf '%s' "$path_in_log"; return; }
  fi

  # 3) last-resort: quick find within provided lib dirs (shallow)
  if [[ ${#LIB_DIRS[@]} -gt 0 ]]; then
    for d in "${LIB_DIRS[@]}"; do
      cand="$(find "$d" -maxdepth 3 -type f -name "$base" 2>/dev/null | head -n1 || true)"
      [[ -n "$cand" ]] && { printf '%s' "$cand"; return; }
    done
  fi

  printf ''
}

print_build_id() {
  local obj="$1"
  local bid
  bid="$("$READELF" -n "$obj" 2>/dev/null | awk '/Build ID/ {print $3; exit}')"
  [[ -n "$bid" ]] && printf ' [build-id:%s]' "$bid" || true
}

# ------------------ Parse memory maps ------------------
declare -a MAP_STARTS MAP_ENDS MAP_OFFS MAP_PATHS
while IFS= read -r mapline; do
  if [[ "$mapline" =~ ^([0-9a-fA-F]+)-([0-9a-fA-F]+)[[:space:]]+([rwxps-]+)[[:space:]]+([0-9a-fA-F]+).*[[:space:]](/[^[:space:]]+)$ ]]; then
    MAP_STARTS+=("0x${BASH_REMATCH[1]}")
    MAP_ENDS+=("0x${BASH_REMATCH[2]}")
    MAP_OFFS+=("0x${BASH_REMATCH[4]}")
    MAP_PATHS+=("${BASH_REMATCH[5]}")
  fi
done < <(grep -i -E '^[0-9a-fA-F]+-[0-9a-fA-F]+' "$CRASH_LOG" || true)

pc_to_relative() {
  local pc_hex="$1" want_path="$2"
  local pc; if [[ "$pc_hex" =~ ^0x ]]; then pc=$((pc_hex)); else pc=$((16#$pc_hex)); fi
  local want_base="$(basename "${want_path%%(*}")"
  for i in "${!MAP_PATHS[@]}"; do
    local p="${MAP_PATHS[$i]}"
    [[ "$(basename "$p")" != "$want_base" ]] && continue
    local s=$(( ${MAP_STARTS[$i]} ))
    local e=$(( ${MAP_ENDS[$i]} ))
    if (( pc >= s && pc < e )); then
      local off=$(( ${MAP_OFFS[$i]} ))
      local base=$(( s - off ))
      local rel=$(( pc - base ))
      printf '0x%x' "$rel"
      return 0
    fi
  done
  return 1
}

# ------------------ Summary tracking ------------------
declare -A THREAD_NAME THREAD_FRAMES THREAD_COUNT
FAULT_TID=""
append_frame() {
  local tid="$1" text="$2"
  local cnt="${THREAD_COUNT[$tid]:-0}"
  if (( cnt < SUMMARY_DEPTH )); then
    THREAD_FRAMES[$tid]="${THREAD_FRAMES[$tid]:-}$text"$'\n'
    THREAD_COUNT[$tid]=$((cnt+1))
  fi
}

current_tid=""
current_tname=""

# ------------------ Process log ------------------
while IFS= read -r rawline; do
  line="$rawline"

  if [[ "$line" =~ ^Fault\ thread\ info: ]]; then
    FAULT_TID=""
    paint_line "$line"
    continue
  fi
  if [[ "$line" =~ ^Tid:([0-9]+),[[:space:]]*Name:([^[:space:]]+) ]]; then
    current_tid="${BASH_REMATCH[1]}"
    current_tname="${BASH_REMATCH[2]}"
    THREAD_NAME[$current_tid]="$current_tname"
    if [[ -z "$FAULT_TID" ]]; then FAULT_TID="$current_tid"; fi
    paint_line "$line"
    continue
  fi

  # Pattern A
  if [[ "$line" =~ ^[[:space:]]*#[0-9]+[[:space:]]+pc[[:space:]]+([0-9a-fA-Fx]+)[[:space:]]+(/[^[:space:]]+) ]]; then
    PC="${BASH_REMATCH[1]}"
    LIB_PATH_RAW="${BASH_REMATCH[2]}"
    OBJ="$(choose_obj "$LIB_PATH_RAW")"
    if [[ -z "$OBJ" ]]; then
      paint_line "$line -> [Object not found in provided lib paths]"
      continue
    fi
    REL=""; [[ "${#MAP_PATHS[@]}" -gt 0 ]] && REL="$(pc_to_relative "$PC" "$LIB_PATH_RAW" || true)"
    OUT=""; if [[ -n "$REL" ]]; then OUT="$(symbolize_pc "$OBJ" "$REL")"; else OUT="$(symbolize_pc "$OBJ" "$PC")"; fi
    if [[ -z "$OUT" || "$OUT" =~ \?\? || "$OUT" =~ ^0x?[0-9a-fA-F]+$ ]]; then
      if [[ "$line" =~ \(([[:alnum:]_:$~<>.-]+)\+([0-9xa-fA-F]+)\) ]]; then
        FUNCOFF="${BASH_REMATCH[1]}+${BASH_REMATCH[2]}"
        ALT="$(resolve_via_funcoff "$OBJ" "$FUNCOFF" || true)"
        [[ -n "$ALT" ]] && OUT="$ALT"
      fi
    fi
    paint_line "$line -> ${OUT:-[unresolved]}"
    [[ -n "$current_tid" ]] && append_frame "$current_tid" "# $(basename "${LIB_PATH_RAW%%(*}") @ ${OUT:-[unresolved]}"
    continue
  fi

  # Pattern B (old style)
  if [[ "$line" =~ ^[[:space:]]*[0-9]+[[:space:]]+([a-zA-Z0-9_.-]+)[[:space:]]+\+[[:space:]]+0x([0-9a-fA-F]+) ]]; then
    LIB_NAME="${BASH_REMATCH[1]}"; OFFSET_HEX="0x${BASH_REMATCH[2]}"
    # Try all lib dirs; do NOT use log path for this legacy form
    OBJ=""
    for d in "${LIB_DIRS[@]}"; do
      [[ -f "$d/$LIB_NAME" ]] && { OBJ="$d/$LIB_NAME"; break; }
    done
    if [[ -z "$OBJ" ]]; then
      paint_line "$line -> [Object not found: $LIB_NAME]"
      continue
    fi
    OUT="$(symbolize_pc "$OBJ" "$OFFSET_HEX")"
    paint_line "$line -> ${OUT:-[unresolved]}"
    [[ -n "$current_tid" ]] && append_frame "$current_tid" "# $LIB_NAME @ ${OUT:-[unresolved]}"
    continue
  fi

  echo "$line"
done < "$CRASH_LOG"

# ------------------ Summary ------------------
echo
[[ $use_color -eq 1 ]] && c_blu
echo "==================== Summary (top ${SUMMARY_DEPTH} frames per thread) ===================="
[[ $use_color -eq 1 ]] && c_reset

print_thread_summary() {
  local tid="$1"
  local name="${THREAD_NAME[$tid]:-?}"
  local hdr="Tid:$tid  Name:$name"
  if [[ "$tid" == "$FAULT_TID" ]]; then
    [[ $use_color -eq 1 ]] && c_red
    echo "🚨 $hdr"
    [[ $use_color -eq 1 ]] && c_reset
  else
    echo "• $hdr"
  fi
  local frames="${THREAD_FRAMES[$tid]:-}"
  if [[ -n "$frames" ]]; then
    while IFS= read -r f; do
      [[ -z "$f" ]] && continue
      if [[ "$f" =~ \#\ ([^[:space:]]+) ]]; then
        base="${BASH_REMATCH[1]}"
        if [[ "$base" =~ $APP_LIB_REGEX ]]; then
          [[ $use_color -eq 1 ]] && c_yel
        elif [[ "$base" =~ ^libWPE(WebKit|.+)\.so$ || "$base" == libWPEWebKit-2.0.so ]]; then
          [[ $use_color -eq 1 ]] && c_cyn
        elif [[ "$base" =~ ^libg(object|lib|io|thread|module|regex|dbus|) || "$base" == libglib-2.0.so || "$base" == libgobject-2.0.so ]]; then
          [[ $use_color -eq 1 ]] && c_mag
        elif [[ "$base" =~ ^libc\.so|^ld-musl|^libm\..*|^libstdc\+\+ ]]; then
          [[ $use_color -eq 1 ]] && c_dim
        fi
      fi
      echo "   $f"
      [[ $use_color -eq 1 ]] && c_reset
    done <<< "$frames"
  else
    echo "   (no frames captured)"
  fi
  echo
}

if [[ -n "$FAULT_TID" && -n "${THREAD_NAME[$FAULT_TID]:-}" ]]; then
  print_thread_summary "$FAULT_TID"
fi
for tid in "${!THREAD_NAME[@]}"; do
  [[ "$tid" == "$FAULT_TID" ]] && continue
  print_thread_summary "$tid"
done


## Old simple symbolizer

##!/bin/bash
#
#if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
#  echo "Usage: $0 <crash_log_file> <unstripped_libs_folder> [tool]"
#  echo "       [tool] (optional): llvm-addr2line or llvm-symbolizer (default: llvm-symbolizer)"
#  exit 1
#fi
#
#OHOS_SDK_NATIVE_LLVM_BIN="${OHOS_SDK_NATIVE}/llvm/bin"
#CRASH_LOG="$1"
#LIBS_FOLDER="$2"
#TOOL="${3:-llvm-symbolizer}" # Default to llvm-symbolizer if not provided
#
#while IFS= read -r line; do
#  if [[ "$line" =~ ^[[:space:]]*[0-9]+[[:space:]]+([a-zA-Z0-9_.-]+)[[:space:]]+\+[[:space:]]+0x([0-9a-fA-F]+) ]]; then
#    LIB_NAME="${BASH_REMATCH[1]}"
#    OFFSET="0x${BASH_REMATCH[2]}"
#
#    UNSTRIPPED_LIB="$LIBS_FOLDER/$LIB_NAME"
#
#    if [[ -f "$UNSTRIPPED_LIB" ]]; then
#      if [[ "$TOOL" == "llvm-addr2line" ]]; then
#        SYMBOL_INFO=$("$OHOS_SDK_NATIVE_LLVM_BIN"/llvm-addr2line -e "$UNSTRIPPED_LIB" -f -C -p "$OFFSET")
#      else
#        SYMBOL_INFO=$("$OHOS_SDK_NATIVE_LLVM_BIN"/llvm-symbolizer --demangle --pretty-print --obj="$UNSTRIPPED_LIB" "$OFFSET")
#      fi
#      echo "$line -> $SYMBOL_INFO"
#    else
#      echo "$line -> [Unstripped library not found]"
#    fi
#    #else
#    #echo "$line"
#  fi
#done <"$CRASH_LOG"
