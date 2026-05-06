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

// Drop a layout into Magic's virtual filesystem
FS.mkdirTree('/work');
FS.writeFile('/work/inv.mag', layoutBytes);

// Run Magic commands — scmos is built into the WASM binary, no tech file needed
runCommand('tech load scmos');
runCommand('load /work/inv');
runCommand('gds write /work/inv');

// Read the result back out
const gdsBytes = FS.readFile('/work/inv.gds');
```

The `scmos` technology family (`scmos`, `minimum`, `nmos`, ...) is embedded in
the WASM binary and available out of the box — those names work without
writing any tech file. To use a custom technology, write its `.tech` file into
the VFS at `/magic/sys/current/<name>.tech` before calling `tech load <name>`.

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
| `sourceFile(path: string)` | Execute a script from the virtual filesystem. Returns 0 on success, -1 if the file could not be opened. |
| `init()`                  | Force initialization. Idempotent — `runCommand` and `sourceFile` call it for you. |
| `update()`                | Drive a display-update cycle. No-op in this headless build. |
| `FS`                      | Emscripten virtual filesystem. See the [Emscripten docs](https://emscripten.org/docs/api_reference/Filesystem-API.html). |

Full TypeScript types ship in [`index.d.ts`](index.d.ts).

### Low-level access

`createMagic()` is a thin convenience wrapper over the underlying Emscripten
module. If you need direct access — for example to call `cwrap` yourself or
to drive `magic_wasm_init` manually — import the module factory directly:

```js
import createMagicModule from 'magic-vlsi-wasm/magic.js';

const module = await createMagicModule({ wasmBinary, print, printErr });
module._magic_wasm_init();
const run = module.cwrap('magic_wasm_run_command', 'number', ['string']);
run('tech load scmos');
```

The bundled examples use this lower-level path together with a small helper
class ([`examples/helpers.js`](examples/helpers.js)) that adds a
`runScript(text)` convenience method — it splits a multi-line Tcl block,
strips comments, and dispatches each line via `runCommand`. Useful when you
have a script as a string rather than as a file in the VFS.

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
npm run example    # extract.js — RC extraction + SPICE netlist
npm run test:gds   # GDS write only
npm run test:drc   # DRC check only
npm run test:cif   # CIF write only
```

Each example loads the bundled [`min.mag`](examples/min.mag) (a small NPN
transistor cell from Magic's own scmos test suite) under the built-in `scmos`
technology — no external tech file required. See [`examples/`](examples/) for
the source; [`example.js`](examples/example.js) is the simplest entry point
(GDS → CIF conversion in ~40 lines).

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

### Bundled test layout

The example layout [`examples/min.mag`](examples/min.mag) is taken from
Magic's own scmos test suite ([`scmos/examples/bipolar/min.mag`](../scmos/examples/bipolar/min.mag))
and is included here as a runnable smoke test for the WASM build. The `scmos`
technology it targets is compiled into the WASM binary, so no external tech
file is shipped.
