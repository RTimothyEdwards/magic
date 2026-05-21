#!/usr/bin/env bash
# Build tcltk/tcl as a static WASM library for linking into magic.wasm.
#
# This script does NOT modify the TCL source tree — the build is fully
# out-of-source. configure is invoked from the build directory inside magic,
# with the TCL source tree referenced through $0's path. All generated files
# (Makefile, objects, tclConfig.sh, libtcl9.x.a, ...) live under $OUT.
#
# Outputs (under $OUT):
#   $OUT/Makefile, *.o, tclConfig.sh, libtcl9.x.a, libtclstub.a
#   $OUT/install/include/{tcl.h, tclDecls.h, ...}
#   $OUT/install/lib/{libtcl9.x.a, libtclstub.a, tclConfig.sh, tcl9.x/<scripts>}
#
# Usage:
#   build-tcl-wasm.sh --src=<TCL source tree> [--out=<build dir>] [--clean]
#
# Requirements: an activated emsdk (emcc/emconfigure/emmake on PATH), a host
# gcc (used to build TCL's minizip helper, which runs natively), make, and a
# git checkout of tcltk/tcl pointed to by --src.
#
# Note on line endings: if the TCL source tree was cloned on Windows with
# git's core.autocrlf=true, unix/configure may have CRLF line endings and
# bash will reject it. Clone with `git -c core.autocrlf=false clone ...` to
# avoid this; magic/npm/build.sh does that automatically.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# magic/ root is two levels up from toolchains/emscripten/.
MAGIC_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

TCL_SRC=""
OUT="$MAGIC_ROOT/build-tcl-wasm"
OPT_CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --src=*)   TCL_SRC=${arg#--src=} ;;
    --out=*)   OUT=${arg#--out=}     ;;
    --clean)   OPT_CLEAN=1           ;;
    *) echo "Unknown option: $arg" >&2; exit 1 ;;
  esac
done

if [ -z "$TCL_SRC" ]; then
  echo "Error: --src=<tcl source tree> is required." >&2
  exit 1
fi
if [ ! -f "$TCL_SRC/unix/configure" ]; then
  echo "Error: $TCL_SRC/unix/configure not found — not a TCL source tree." >&2
  exit 1
fi
# Detect CRLF in configure up-front so the failure mode is a useful error and
# not a cryptic `set: pipefail: invalid option name` from bash.
if head -1 "$TCL_SRC/unix/configure" | grep -q $'\r'; then
  echo "Error: $TCL_SRC/unix/configure has CRLF line endings." >&2
  echo "       Reclone tcltk/tcl with: git -c core.autocrlf=false clone …" >&2
  exit 1
fi

# Normalise to absolute paths so the generated tclConfig.sh has stable paths.
TCL_SRC=$(cd "$TCL_SRC" && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

if [ $OPT_CLEAN -eq 1 ]; then
  rm -rf "$OUT"
  mkdir -p "$OUT"
fi

command -v emcc >/dev/null 2>&1 || {
  echo "Error: emcc not on PATH. Activate emsdk first." >&2
  exit 1
}
echo "Using emcc: $(command -v emcc)"
emcc --version | head -1

ncpu() {
  if command -v nproc >/dev/null 2>&1; then nproc
  else echo 2
  fi
}

cd "$OUT"

# --- configure --------------------------------------------------------------
# Standard autoconf out-of-source pattern: invoke configure from the build
# dir via its absolute path. configure uses dirname($0) as srcdir.
#
# -sUSE_ZLIB=1 makes emcc inject its zlib port so configure finds zlib.h /
# deflateSetHeader on the link line. Without it, TCL falls back to its
# compat/zlib copy whose include path isn't picked up by tclEvent.o.
#
# --disable-shared      we statically link libtcl into magic.wasm.
# --disable-load        no dynamic loading inside wasm.
# --enable-symbols=no   release (-O2). Override CFLAGS to add -g for debug.
if [ ! -f Makefile ]; then
  echo "=== emconfigure ==="
  CFLAGS="-O2 -sUSE_ZLIB=1" \
    emconfigure "$TCL_SRC/unix/configure" \
      --disable-shared \
      --disable-load \
      --enable-symbols=no \
      --host=wasm32-unknown-emscripten \
      --prefix="$OUT/install"
fi

# --- build ------------------------------------------------------------------
# HOST_CC/AR/RANLIB must be native — TCL's Makefile builds a `minizip` tool
# that runs on the host to produce the embedded zipfs resource. Without this
# override emcc would build minizip itself as a wasm module, which crashes on
# small stacks. HOST_OBJEXT is kept distinct from OBJEXT so host and target
# objects don't collide.
#
# Archive names come from tclConfig.sh so we don't bake in a fixed version.
TCL_LIB_FILE=$(. "$OUT/tclConfig.sh" && echo "$TCL_LIB_FILE")
TCL_STUB_LIB_FILE=$(. "$OUT/tclConfig.sh" && echo "$TCL_STUB_LIB_FILE")
HOST_OBJEXT=hostobj

echo "=== build ($TCL_LIB_FILE, $TCL_STUB_LIB_FILE) ==="
emmake make -j"$(ncpu)" \
  HOST_CC=gcc HOST_AR=ar HOST_RANLIB=ranlib HOST_EXEEXT= HOST_OBJEXT="$HOST_OBJEXT" \
  "$TCL_LIB_FILE" "$TCL_STUB_LIB_FILE"

# --- install ----------------------------------------------------------------
# install-headers populates $OUT/install/include with tcl.h + friends.
# install-libraries copies the Tcl script library (init.tcl, encodings, ...)
# under $OUT/install/lib/tcl9.x. We skip install-binaries because it would
# build a tclsh executable that we don't need; we cp the static archives
# manually so magic's configure (which scans <prefix>/lib for tclConfig.sh
# and a libtcl*.a) finds everything in one place.
emmake make \
  HOST_CC=gcc HOST_AR=ar HOST_RANLIB=ranlib HOST_EXEEXT= HOST_OBJEXT="$HOST_OBJEXT" \
  install-headers install-libraries

mkdir -p "$OUT/install/lib"
cp -f "$TCL_LIB_FILE" "$TCL_STUB_LIB_FILE" tclConfig.sh "$OUT/install/lib/"

echo
echo "=== artifacts ==="
ls -la "$TCL_LIB_FILE" "$TCL_STUB_LIB_FILE" tclConfig.sh 2>&1 | sed 's/^/  /'
echo "  install/lib:"
ls -la install/lib 2>&1 | sed 's/^/    /'
