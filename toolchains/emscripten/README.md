# Magic VLSI — Headless WASM Build

This toolchain builds Magic as a headless WebAssembly module using Emscripten.
X11, Tk, OpenGL, and readline are all disabled. The resulting `magic.js` /
`magic.wasm` pair can be loaded in Node.js, a browser, or a Web Worker.

## Quick start (npm package)

The easiest way to build and use the WASM module is through the npm package:

```bash
# Build magic.js + magic.wasm and copy them into npm/
bash npm/build.sh

# Run the test suite (extract, GDS, DRC, CIF)
npm --prefix npm test
```

See [`npm/examples/`](../../npm/examples/) for usage examples.

## Manual build

Prerequisites: an activated [emsdk](https://emscripten.org/docs/getting_started/downloads.html)
checkout (`emcc`, `emar`, `emranlib` on `PATH`), plus standard `make` and `gcc`.

```bash
# 1. Configure for Emscripten
CFLAGS="--std=c17 -D_DEFAULT_SOURCE=1 -DEMSCRIPTEN=1 -g" \
  emconfigure ./configure \
    --without-cairo --without-opengl --without-x --without-tk --without-tcl \
    --disable-readline --disable-compression \
    --host=asmjs-unknown-emscripten \
    --target=asmjs-unknown-emscripten

# 2. Append the Emscripten-specific make settings
cat toolchains/emscripten/defs.mak >> defs.mak

# 3. Build
emmake make depend
emmake make -j$(nproc) modules libs
emmake make techs
emmake make mains
```

The outputs are `magic/magic.js` and `magic/magic.wasm`.

## Embedded files

The following runtime files are baked directly into the WASM binary via
Emscripten's `--embed-file` mechanism and are available at startup without
any host filesystem access:

| Host path | VFS path |
|-----------|----------|
| `scmos/` | `/magic/sys/current/` |
| `windows/windows7.glyphs` | `/magic/sys/windows7.glyphs` |
| `windows/windows7.glyphs` | `/magic/sys/bw.glyphs` |

To embed a custom technology file, add an `--embed-file` entry to
`TOP_EXTRA_LIBS` in [`defs.mak`](defs.mak).

## Exported C API

The WASM module exports four functions:

| Function | Description |
|----------|-------------|
| `magic_wasm_init()` | Initialize Magic (idempotent — safe to call multiple times). Returns 0 on success. |
| `magic_wasm_run_command(const char *cmd)` | Dispatch one Magic command. Calls `magic_wasm_init()` automatically if needed. Returns 0 on success. |
| `magic_wasm_source_file(const char *path)` | Read and execute a command file from the virtual filesystem. |
| `magic_wasm_update()` | Drive a display-update cycle. No-op in headless builds (null display suspends all redraws). |

### JavaScript usage

```js
import createMagic from 'magic-vlsi-wasm';

const { runCommand, FS } = await createMagic();

// Write a layout file into the virtual filesystem
FS.writeFile('/work/inv.mag', layoutBytes);

// Run Magic commands
runCommand('tech load sky130A');
runCommand('load /work/inv');
runCommand('gds write /work/inv');

// Read the result back out
const gdsBytes = FS.readFile('/work/inv.gds');
```

## Notes

- `CAD_ROOT` is automatically set to `/` so that embedded system files are
  resolved under `/magic/sys/`.
- The null display driver (`-d null`) sets `GrDisplayStatus = DISPLAY_SUSPEND`,
  which causes `WindUpdate` to return immediately without invoking any display
  callbacks. This is what makes the WASM build safe to run without a screen.
- All POSIX signal/timer APIs (`setitimer`, `SIGALRM`, `fcntl`) are compiled
  out under `__EMSCRIPTEN__`; the display progress timer becomes a no-op.
