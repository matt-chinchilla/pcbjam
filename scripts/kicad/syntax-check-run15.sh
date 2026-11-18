#!/usr/bin/env bash
# Copyright (C) 2026 Circuit Center Inc
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Engine run 15: the syntax only compile (em++ -fsyntax-only) an author runs before handing over,
# with the build's EXACT flags and NO build: inside the builder image, the build cache volume
# mounted READ ONLY (config.h, the generated lexers, wx's setup, compile_commands.json), the
# worktrees mounted read only, nothing written anywhere but the container's own layer.
#
#   scripts/kicad/syntax-check-run15.sh --kicad <kicad worktree> [--wx <wxWidgets tree>]
#       [all | pcbnew eeschema symedit fpedit dsedit kicad_editor occ ...]
#       [--kicad-file <path in kicad> ...]
#
# Run it from the pcbjam worktree whose bindings are checked (this script's own repository),
# always through the machine's gate: /home/matthew/cc-wave2/bin/gate.sh scripts/kicad/...
#
# The binding TUs are compiled by build-kicad-target.sh's OWN compile_embind_tu and its OWN
# merged image call list (the kicad_editor branch of step 7.1), both read from that script at
# run time and never retyped, with EXTRA_FLAGS, KICAD_TU_ABI_FLAGS and WX_CXXFLAGS as the Debug
# build sets them; em++ then gets -fsyntax-only in place of -c and -o. "occ" is pcbnew_embind.cpp
# as occ_service compiles it (standalone: no -DKICAD_MERGED_EMBIND). A KiCad source is
# compiled with its own compile_commands.json line (the precompiled header flags dropped; its
# --use-port=zlib makes emcc fetch that port into the container's layer, so that mode alone has
# the network).
# It refuses while an engine build runs (the build rewrites the cache volume).
set -euo pipefail

ENGINE_ROOT="${CC_ENGINE_ROOT:-/home/matthew/cc-engine}"
IMAGE="${CC_BUILDER_IMAGE:-kicad-wasm-cc-engine-kicad-wasm-builder:latest}"
VOLUME="${CC_BUILD_CACHE:-kicad-wasm-cc-engine_kicad-build-cache}"
PCBJAM="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KICAD=""
WX="$ENGINE_ROOT/tree-cc8/wxwidgets"
TUS=()
KFILES=()

die() { echo "syntax-check-run15: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
  case "$1" in
    --kicad) KICAD="${2:?}"; shift 2 ;;
    --wx) WX="${2:?}"; shift 2 ;;
    --kicad-file) KFILES+=("${2:?}"); shift 2 ;;
    all) TUS+=(pcbnew eeschema symedit fpedit dsedit kicad_editor occ); shift ;;
    pcbnew|eeschema|symedit|fpedit|dsedit|kicad_editor|occ) TUS+=("$1"); shift ;;
    *) die "unknown argument $1" ;;
  esac
done

[ -n "$KICAD" ] || die "--kicad <kicad worktree> is required"
KICAD="$(cd "$KICAD" && pwd)"
WX="$(cd "$WX" && pwd)"
[ -f "$KICAD/CMakeLists.txt" ] || die "$KICAD is not a KiCad tree"
[ -f "$WX/include/wx/wx.h" ] || die "$WX is not a wxWidgets tree"
[ ${#TUS[@]} -gt 0 ] || [ ${#KFILES[@]} -gt 0 ] || die "name the TUs (or all) and/or --kicad-file paths"

# Never while a build runs: the recipe's container is up, or the build's pid is alive.
if [ -n "$(docker ps -q --filter label=com.docker.compose.project=kicad-wasm-cc-engine)" ]; then
  die "an engine build is running (the kicad-wasm-cc-engine container is up); wait for it"
fi
for pidfile in "$ENGINE_ROOT"/out/*/build.pid; do
  [ -f "$pidfile" ] || continue
  pid="$(cat "$pidfile" 2>/dev/null || true)"
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null && grep -q "build.sh" "/proc/$pid/cmdline" 2>/dev/null; then
    die "an engine build is running (pid $pid in $pidfile); wait for it"
  fi
done

# The inner run: everything it needs comes from the mounted build script.
INNER='
set -euo pipefail
PROJECT_ROOT=/workspace
KICAD_DIR=$PROJECT_ROOT/kicad
BUILD_ROOT=$PROJECT_ROOT/build-wasm
SYSROOT=$BUILD_ROOT/sysroot
WX_BUILD=$BUILD_ROOT/wxwidgets
STUBS_DIR=$PROJECT_ROOT/wasm/stubs
STUBS_BUILD=/tmp/stubs
APP_NAME=kicad_editor
KICAD_BUILD=$BUILD_ROOT/kicad-$APP_NAME
mkdir -p "$STUBS_BUILD"
SCRIPT=$PROJECT_ROOT/scripts/kicad/build-kicad-target.sh
log_info() { :; }
emmake() { :; }
eval "$(grep "^export DEPS_EH_FLAGS=" $PROJECT_ROOT/scripts/common/env.sh)"
KICAD_EH_FLAGS="$DEPS_EH_FLAGS"
# The Debug branch of step 5 (the build runs in Debug: "Building KiCad in DEBUG mode").
eval "$(awk "/DEBUG_BUILD:-0/{f=1} f&&/^ *EXTRA_FLAGS=/{print; n++} f&&/^ *EMBIND_CONFIG_DEFINES=/{print; n++} n==2{exit}" $SCRIPT)"
eval "$(grep "^KICAD_TU_ABI_FLAGS=" $SCRIPT)"
eval "$(grep "^WX_CXXFLAGS=" $SCRIPT)"
# compile_embind_tu, verbatim.
eval "$(awk "/^compile_embind_tu\\(\\) \\{/{f=1} f{print} f&&/^}/{exit}" $SCRIPT)"
# The merged image call list, verbatim (step 7.1, the kicad_editor branch).
CALLS="$(awk "/Compiling merged Embind bindings/{f=1;next} f&&/^elif /{exit} f{print}" $SCRIPT)"
[ -n "$CALLS" ] || { echo "the merged call list was not found in $SCRIPT" >&2; exit 2; }
em++() {
  local out=() skip=0 a
  for a in "$@"; do
    if [ $skip = 1 ]; then skip=0; continue; fi
    case "$a" in -c) ;; -o) skip=1 ;; *) out+=("$a") ;; esac
  done
  command em++ -fsyntax-only "${out[@]}"
}
WANT=" $CC_TUS "
fails=0
eval "$(declare -f compile_embind_tu | sed "1s/compile_embind_tu/build_compile_embind_tu/")"
ms() { echo $(( ( $(date +%s%N) - $1 ) / 1000000 )); }
compile_embind_tu() {
  local tu t0; tu=$(basename "$1" _embind.cpp); t0=$(date +%s%N)
  case "$WANT" in *" $tu "*) ;; *) return 0 ;; esac
  if build_compile_embind_tu "$@"; then
    printf "PASS %-14s %s subdir=%s %s (%s ms)\n" "$tu" "$(basename "$1")" "$3" "${4:-}" "$(ms $t0)"
  else
    printf "FAIL %-14s %s subdir=%s %s\n" "$tu" "$(basename "$1")" "$3" "${4:-}"
    fails=$((fails+1))
  fi
}
eval "$CALLS"
# occ: pcbnew_embind.cpp as occ_service links it (EMBIND_APP pcbnew, the include subdir pcbnew,
# NO -DKICAD_MERGED_EMBIND: the single app branch of step 7.1).
case "$WANT" in *" occ "*)
  t0=$(date +%s%N)
  if build_compile_embind_tu "$PROJECT_ROOT/wasm/bindings/pcbnew_embind.cpp" "$STUBS_BUILD/pcbnew_embind.o" pcbnew; then
    printf "PASS %-14s %s subdir=%s (%s ms)\n" occ pcbnew_embind.cpp pcbnew "$(ms $t0)"
  else
    printf "FAIL %-14s %s subdir=%s\n" occ pcbnew_embind.cpp pcbnew; fails=$((fails+1))
  fi ;;
esac
for f in $CC_KFILES; do
  if python3 - "$KICAD_BUILD/compile_commands.json" "/workspace/kicad/$f" > /tmp/cmd <<"PY"
import json, shlex, sys
db = json.load(open(sys.argv[1]))
hits = [e for e in db if e["file"] == sys.argv[2]]
if not hits:
    sys.exit("no compile_commands.json line for " + sys.argv[2] + " (a new source needs its CMake line first)")
e = hits[0]
args = shlex.split(e["command"]) if "command" in e else e["arguments"]
out = []
i = 0
while i < len(args):
    a = args[i]
    if a in ("-o", "-MF", "-MT", "-MQ"):
        i += 2; continue
    if a in ("-c", "-MD", "-MMD"):
        i += 1; continue
    if a == "-Xclang" and i + 3 < len(args) and args[i + 1] in ("-include-pch", "-include") and "cmake_pch" in args[i + 3]:
        i += 4; continue
    if a == "-include" and i + 1 < len(args) and "cmake_pch" in args[i + 1]:
        i += 2; continue
    out.append(a); i += 1
if out and out[0].endswith("ccache"):
    out = out[1:]
print(e["directory"])
print(shlex.join(out + ["-fsyntax-only"]))
PY
  then
    dir=$(sed -n 1p /tmp/cmd); cmd=$(sed -n 2p /tmp/cmd)
    t0=$(date +%s%N)
    if (cd "$dir" && eval "$cmd"); then
      printf "PASS kicad %s (%s ms)\n" "$f" "$(ms $t0)"
    else
      printf "FAIL kicad %s\n" "$f"; fails=$((fails+1))
    fi
  else
    printf "FAIL kicad %s (no compile line)\n" "$f"; fails=$((fails+1))
  fi
done
echo "syntax-check: $fails failed"
[ $fails = 0 ]
'

# The binding TUs need no network. A KiCad source's line carries --use-port=zlib, whose port
# emcc fetches into the container's own (thrown away) cache: network only for --kicad-file.
NET=(--network none)
[ ${#KFILES[@]} -gt 0 ] && NET=()

exec docker run --rm "${NET[@]}" \
  -e CC_TUS="${TUS[*]:-}" -e CC_KFILES="${KFILES[*]:-}" \
  -v "$VOLUME:/workspace/build-wasm:ro" \
  -v "$PCBJAM/wasm:/workspace/wasm:ro" \
  -v "$PCBJAM/scripts:/workspace/scripts:ro" \
  -v "$KICAD:/workspace/kicad:ro" \
  -v "$WX:/workspace/wxwidgets:ro" \
  "$IMAGE" bash -c "$INNER"
