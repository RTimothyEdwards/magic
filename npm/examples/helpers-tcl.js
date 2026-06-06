// Shared utilities for Magic WASM TCL-variant examples.
//
// Loads the TCL-enabled WASM variant.  Scripts use magic:: prefixed commands;
// runScript() evaluates them through the Tcl interpreter via Tcl_EvalFile.
import createMagicModule from '../tcl/magic.js';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve, basename } from 'node:path';

export const EXAMPLES_DIR = dirname(fileURLToPath(import.meta.url));
export const wasmBinary   = readFileSync(resolve(EXAMPLES_DIR, '../tcl/magic.wasm'));
export const DEFAULT_TECH = 'scmos';
export const DEFAULT_MAG  = resolve(EXAMPLES_DIR, 'min.mag');
export const DEFAULT_OUT  = resolve(EXAMPLES_DIR, 'output-tcl');

export { basename };

export function vfsWrite(FS, vfsPath, source) {
  const dir = vfsPath.substring(0, vfsPath.lastIndexOf('/'));
  if (dir) FS.mkdirTree(dir);
  FS.writeFile(vfsPath, typeof source === 'string' ? readFileSync(source) : source);
}

export function vfsRead(FS, vfsPath) {
  try { return Buffer.from(FS.readFile(vfsPath)); } catch { return null; }
}

export class MagicWasm {
  constructor(mod) {
    this._init       = mod.cwrap('magic_wasm_init',          'number', []);
    this._run        = mod.cwrap('magic_wasm_run_command',   'number', ['string']);
    this._sourceFile = mod.cwrap('magic_wasm_source_file',   'number', ['string']);
    this.FS          = mod.FS;
  }
  init() {
    const rc = this._init();
    if (rc !== 0) throw new Error(`magic_wasm_init failed with code ${rc}`);
  }
  // Evaluate a Tcl script (with magic:: prefixed commands) through the
  // Tcl interpreter.  Writes the script to VFS then calls magic_wasm_source_file,
  // which in TCL mode uses Tcl_EvalFile.
  runScript(text) {
    const path = '/tmp/_magic_script.tcl';
    this.FS.mkdirTree('/tmp');
    this.FS.writeFile(path, text);
    try {
      this._sourceFile(path);
    } catch (e) {
      // A WASM trap during Tcl evaluation otherwise gives no hint of the
      // source; flag it before the error propagates.
      console.error('[magic-tcl] script evaluation failed');
      throw e;
    }
  }
  // Evaluate a Tcl expression directly (needed for proc definitions in PCell).
  runTcl(text) {
    this._run(text);
  }
}

export async function createMagic({ onPrint, onPrintErr } = {}) {
  const lines = [];
  const mod = await createMagicModule({
    wasmBinary,
    print:    onPrint    ?? (msg => { lines.push(msg); console.log('[magic-tcl]', msg); }),
    printErr: onPrintErr ?? (msg => { lines.push(msg); console.error('[magic-tcl]', msg); }),
  });
  const magic = new MagicWasm(mod);
  magic.init();
  return { magic, lines };
}

export function loadCell(FS, tech, magFile) {
  const cell = basename(magFile, '.mag');
  vfsWrite(FS, `/work/${cell}.mag`, magFile);
  return { tech, cell };
}

export function loadScript(name, tech, cell) {
  return readFileSync(resolve(EXAMPLES_DIR, name), 'utf8')
    .replaceAll('__TECH__', tech)
    .replaceAll('__CELL__', cell);
}

// Print a failure with enough detail to be actionable in CI.  WASM aborts
// surface as a RuntimeError whose .message is terse while .stack carries the
// wasm-function offsets that emsymbolizer maps back to C source — so always
// prefer the stack.
export function reportError(e) {
  console.error(e?.stack ?? String(e?.message ?? e));
}
