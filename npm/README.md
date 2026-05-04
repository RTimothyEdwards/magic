# magic-vlsi-wasm

[Magic VLSI](http://opencircuitdesign.com/magic/) layout tool, compiled to
WebAssembly as a headless library. Runs in Node.js, browsers, and Web Workers
— no X11, no Tk, no native dependencies.

Use it to programmatically read and write `.mag`, `.gds`, `.cif`, `.ext`, and
SPICE netlists; run DRC; extract parasitics — anywhere JavaScript runs.

## Install

```bash
npm install magic-vlsi-wasm
```

Requires Node.js 18 or newer.

## Quick start

```js
import createMagic from 'magic-vlsi-wasm';

const { runCommand, FS } = await createMagic();

// Write a layout into Magic's virtual filesystem
FS.writeFile('/work/inv.mag', layoutBytes);

// Run Magic commands
runCommand('tech load minimum');
runCommand('load /work/inv');
runCommand('gds write /work/inv');

// Read the result back out
const gdsBytes = FS.readFile('/work/inv.gds');
```

The `scmos` technology family (`scmos`, `minimum`, `nmos`, ...) is embedded in
the WASM binary and available out of the box. Custom tech files can be written
into the VFS at `/magic/sys/current/<name>.tech`.

## API

```ts
createMagic(options?): Promise<MagicInstance>
```

`options` is forwarded to the underlying Emscripten module. Useful keys:

| Key          | Default          | Purpose |
|--------------|------------------|---------|
| `wasmBinary` | fetched lazily   | Pre-fetched ArrayBuffer of `magic.wasm` (skips a network round-trip in browsers) |
| `print`      | `console.log`    | Callback for each stdout line |
| `printErr`   | `console.error`  | Callback for each stderr line |

The returned `MagicInstance` exposes:

| Method                    | Description |
|---------------------------|-------------|
| `runCommand(cmd: string)` | Dispatch a single Magic command. Returns 0 on success. |
| `sourceFile(path: string)` | Execute a script from the virtual filesystem. |
| `init()`                  | Force initialization. Idempotent — `runCommand` and `sourceFile` call it for you. |
| `update()`                | Drive a display-update cycle. No-op in this headless build. |
| `FS`                      | Emscripten virtual filesystem. See the [Emscripten docs](https://emscripten.org/docs/api_reference/Filesystem-API.html). |

Full TypeScript types ship in [`index.d.ts`](index.d.ts).

## Examples

The package ships runnable examples for the most common workflows. After
installing, run one directly:

```bash
node node_modules/magic-vlsi-wasm/examples/extract.js
node node_modules/magic-vlsi-wasm/examples/gds.js
node node_modules/magic-vlsi-wasm/examples/drc.js
node node_modules/magic-vlsi-wasm/examples/cif.js
```

Or, when developing inside this repo:

```bash
npm test           # full suite (extract, gds, drc, cif)
npm run test:gds   # GDS write only
npm run test:drc   # DRC check only
npm run test:cif   # CIF write only
```

Each example is self-contained and reads `examples/siliwiz.{mag,tech}` by
default. See [`examples/`](examples/) for the source.

## Build from source

If you want to rebuild the WASM module yourself, see
[`toolchains/emscripten/README.md`](../toolchains/emscripten/README.md).
The short version:

```bash
bash npm/build.sh           # debug build, copies magic.js + magic.wasm into npm/
bash npm/build.sh --release # optimized
bash npm/build.sh --test    # build + run tests
bash npm/build.sh --pack    # build + produce magic-vlsi-wasm-<version>.tgz
```

You will need an activated [emsdk](https://emscripten.org/docs/getting_started/downloads.html)
checkout (Magic pins emsdk `3.1.56` — see the comment in `npm/build.sh`).

## Limitations

- Headless only. There is no display driver, so commands that draw to a
  window (`view`, `findbox`, interactive macros) are no-ops.
- WASM memory starts at 32 MB and grows as needed. Very large GDS imports
  may need `INITIAL_MEMORY` bumped (rebuild required).
- Single-threaded. WASM modules are not thread-safe — create one instance
  per worker.

## License

[HPND](LICENSE) — Copyright (C) 1985, 1990 Regents of the University of California.

### Bundled test technology

The example layout (`examples/siliwiz.mag`) and technology file
(`examples/siliwiz.tech`) are derived from the
[SiliWiz](https://github.com/wokwi/siliwiz) educational silicon design
tool. They are bundled here only as a runnable smoke test for the WASM
build. The technology file is © R. Timothy Edwards, Open Circuit Design,
2023, marked by the author as containing no proprietary information.
