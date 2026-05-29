# magic-vlsi-wasm

[Magic VLSI](http://opencircuitdesign.com/magic/) layout tool, compiled to
WebAssembly as a headless library. Runs in Node.js, browsers, and Web Workers
— no X11, no Tk, no native dependencies.

Use it to programmatically read and write `.mag`, `.gds`, `.cif`, `.ext`, and
SPICE netlists; run DRC; extract parasitics — anywhere JavaScript runs.

The package ships two variants:

| Variant | Entry point | Description |
|---------|-------------|-------------|
| **notcl** (default) | `magic-vlsi-wasm` | Standalone — no Tcl interpreter. Commands are plain Magic command strings. |
| **tcl** | `magic-vlsi-wasm/tcl` | Embeds a full Tcl 9 interpreter. Commands are evaluated as Tcl; Magic commands are available as the `::magic::` ensemble. |

## Install

The package is published to GitHub Packages. Add the following to your
project's `.npmrc` so npm knows where to find it:

```
@rtimothyedwards:registry=https://npm.pkg.github.com
```

Then install:

```bash
npm install @rtimothyedwards/magic-vlsi-wasm
```

If the package is private or you hit a 401, authenticate with a GitHub
[personal access token](https://github.com/settings/tokens) that has the
`read:packages` scope:

```
//npm.pkg.github.com/:_authToken=YOUR_TOKEN
@rtimothyedwards:registry=https://npm.pkg.github.com
```

Requires Node.js 18 or newer.

## Quick start

### Default variant (no Tcl)

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

### TCL variant

```js
import createMagic from 'magic-vlsi-wasm/tcl';

const { runCommand, FS } = await createMagic();

// Pure Tcl works directly
runCommand('set x 42');
runCommand('puts $tcl_version');

// Magic commands are available as the ::magic:: ensemble
runCommand('magic::tech load scmos');
runCommand('magic::load /work/inv');
runCommand('magic::gds write /work/inv');
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
bash npm/build.sh                    # both variants, debug build
bash npm/build.sh --variant=notcl    # default variant only (faster)
bash npm/build.sh --variant=tcl      # TCL variant only
bash npm/build.sh --release          # optimized (-O2, no debug symbols)
bash npm/build.sh --test             # build + run tests
bash npm/build.sh --pack             # build + produce magic-vlsi-wasm-<version>.tgz
```

You will need an activated [emsdk](https://emscripten.org/docs/getting_started/downloads.html)
on your PATH. If you pass `EMSDK_DIR=/path/to/emsdk`, `build.sh` sources
`emsdk_env.sh` for you.

### TCL variant: cloning the TCL source tree

The TCL variant links against a static WASM build of
[tcltk/tcl](https://github.com/tcltk/tcl). `build.sh` clones the source tree
automatically into `build-tcl-wasm/tcl` on the first run and builds it
out-of-source in the same directory — everything stays under `build-tcl-wasm/`
(which is gitignored), so nothing outside it is touched. Subsequent runs reuse
the existing clone and build.

```bash
# Override the TCL version or source repository
TCL_REF=core-9-0-3 bash npm/build.sh --variant=tcl
TCL_REPO_URL=https://github.com/tcltk/tcl.git bash npm/build.sh --variant=tcl
```

CI always resolves the latest stable `core-9-0-x` tag automatically. To build
against a specific version, set `TCL_REF` in the environment.

## Versioning

Published versions follow the scheme `{MAJOR}.{MINOR}.{PATCH}0{YYYYMMDD}+git{SHA}`,
for example `8.3.799020261231+git01234cde`.

The date is embedded directly into the patch number (separated by a leading
zero for readability). This means:

- Versions are orderable numerically — a later build date is always a higher
  version number within the same patch series.
- Security or bugfix releases for `8.3.799` can be inserted as later dates
  (`8.3.799020270101`, `8.3.799020270201`, …) without bumping the patch number.
- Users who want to lock to the `8.3.799` series and receive only those patches
  can use the range `<=8.3.799900000000` or `<8.3.8000000000000`.
- `~8.3.799` matches all `8.3.*` versions (broader than the 799 series alone).

The `+git…` suffix is build metadata — it is ignored by npm for version
comparison and range matching. It exists purely for traceability.

## Limitations

- Headless only. There is no display driver, so commands that draw to a
  window (`view`, `findbox`, interactive macros) are no-ops.
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
