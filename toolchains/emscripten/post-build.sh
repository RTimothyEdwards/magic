#!/usr/bin/env bash
# Post-process Emscripten output for Node.js 22+ ES module compatibility.
#
# Emscripten (3.1.x) produces ES module output that still references
# `require()`, `__dirname`, and `__filename` — all of which are undefined
# in pure ESM. This script patches magic.js to:
#   1. Alias every `require("x")` call to `___cr("x")` (createRequire).
#   2. Inject ESM-safe `__filename` / `__dirname` / `require` at module top.
#   3. Sync `___emscripten_embedded_file_data` from the .wasm global.
#
# Usage:  post-build.sh <magic.js> <magic.wasm>
#
# Idempotent — safe to run multiple times.
#
# WARNING: These patches depend on exact text patterns emitted by a specific
# Emscripten version. If you upgrade emsdk, verify the patches still apply
# (see .github/workflows/main.yml for the pinned version).

set -euo pipefail

if [ $# -ne 2 ]; then
    echo "Usage: $0 <magic.js> <magic.wasm>" >&2
    exit 1
fi

JS=$1
WASM=$2

if [ ! -f "$JS" ] || [ ! -f "$WASM" ]; then
    echo "Error: $JS or $WASM not found." >&2
    exit 1
fi

# Portable in-place sed: BSD (macOS) and GNU diverge on -i.
# Using a temp file + mv keeps both happy without relying on -i at all.
_sed_inplace() {
    local expr=$1 file=$2
    local tmp
    tmp=$(mktemp)
    sed "$expr" "$file" > "$tmp"
    mv "$tmp" "$file"
}

# --- 1 & 2. ES module compatibility -----------------------------------------
if ! grep -q 'createRequire' "$JS"; then
    echo "[post-build] Injecting ESM createRequire + __filename/__dirname shims"
    _sed_inplace 's/require("\([^"]*\)")/___cr("\1")/g' "$JS"
    _sed_inplace '1s|^|\nimport{createRequire as ___cr}from"module";\nimport{fileURLToPath as ___fup}from"url";\nimport{dirname as ___dn}from"path";\nvar __filename=___fup(import.meta.url);\nvar __dirname=___dn(__filename);\n|' "$JS"
fi

# Ensure a top-level `require` binding exists (Emscripten's environment probe
# does `typeof require == "function"`, which would otherwise be "undefined").
# Skip if Emscripten already emits one (newer versions do).
if ! grep -qE 'var[[:space:]]+require[[:space:]]*=' "$JS" \
   && grep -q 'import{createRequire as ___cr}from"module";' "$JS"; then
    echo "[post-build] Adding top-level require binding"
    _sed_inplace 's|import{createRequire as ___cr}from"module";|import{createRequire as ___cr}from"module";var require=___cr(import.meta.url);|' "$JS"
fi

# --- 3. Sync ___emscripten_embedded_file_data from .wasm --------------------
# Emscripten sometimes bakes a stale constant into magic.js after an
# incremental rebuild. Read the true value from the WASM global section
# (section id 6, global index 1, i32.const opcode 0x41, signed LEB128).
python3 - "$JS" "$WASM" <<'PY'
import sys, re

js_path, wasm_path = sys.argv[1], sys.argv[2]
data = open(wasm_path, 'rb').read()

def read_uleb(buf, pos):
    val = shift = 0
    while True:
        b = buf[pos]; pos += 1
        val |= (b & 0x7f) << shift
        shift += 7
        if not (b & 0x80):
            return val, pos

def read_sleb(buf, pos):
    val = shift = 0
    while True:
        b = buf[pos]; pos += 1
        val |= (b & 0x7f) << shift
        shift += 7
        if not (b & 0x80):
            if b & 0x40:
                val |= (~0) << shift
            return val & 0xffffffff, pos

# Walk sections looking for section id 6 (Global)
pos = 8  # skip magic + version
actual = None
while pos < len(data):
    sid = data[pos]; pos += 1
    size, pos = read_uleb(data, pos)
    end = pos + size
    if sid == 6:
        count, p = read_uleb(data, pos)
        for gi in range(count):
            p += 2  # valtype + mutability
            opcode = data[p]; p += 1
            if opcode == 0x41:  # i32.const
                val, p = read_sleb(data, p)
                if gi == 1:
                    actual = val
                    break
            p += 1  # skip end opcode
        break
    pos = end

if actual is None:
    sys.stderr.write(
        '[post-build] WARN: could not parse global index 1 from wasm; '
        'skipping ___emscripten_embedded_file_data sync\n')
    sys.exit(0)

js = open(js_path).read()
m = re.search(r'Module\[.___emscripten_embedded_file_data.\]=(\d+)', js)
if not m:
    # Emscripten may have fixed the stale-constant bug in a newer version.
    # Not fatal, but worth surfacing so we can drop this patch eventually.
    sys.stderr.write(
        '[post-build] INFO: ___emscripten_embedded_file_data not present; '
        'sync patch no longer applies (likely fixed upstream)\n')
    sys.exit(0)

old = int(m.group(1))
if old != actual:
    print(f'[post-build] Fixing ___emscripten_embedded_file_data: {old} -> {actual}')
    js = js.replace(
        f'Module["___emscripten_embedded_file_data"]={old}',
        f'Module["___emscripten_embedded_file_data"]={actual}',
    )
    open(js_path, 'w').write(js)
PY

echo "[post-build] Done."
