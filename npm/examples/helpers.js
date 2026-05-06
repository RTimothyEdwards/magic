// Shared utilities for Magic WASM examples.
import createMagicModule from '../magic.js';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve, basename } from 'node:path';

export const EXAMPLES_DIR = dirname(fileURLToPath(import.meta.url));
export const wasmBinary   = readFileSync(resolve(EXAMPLES_DIR, '../magic.wasm'));
export const DEFAULT_TECH = 'scmos';
export const DEFAULT_MAG  = resolve(EXAMPLES_DIR, 'min.mag');
export const DEFAULT_OUT  = resolve(EXAMPLES_DIR, 'output');

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
    this._init = mod.cwrap('magic_wasm_init',        'number', []);
    this._run  = mod.cwrap('magic_wasm_run_command', 'number', ['string']);
    this.FS    = mod.FS;
  }
  init() {
    const rc = this._init();
    if (rc !== 0) throw new Error(`magic_wasm_init failed with code ${rc}`);
  }
  runScript(text) {
    for (const line of text.split('\n')) {
      const l = line.trim();
      if (l && !l.startsWith('#')) this._run(l);
    }
  }
}

// Creates a fresh Magic WASM instance and calls init().
// onPrint / onPrintErr default to console.log/error with a [magic] prefix.
// All output lines are also collected into the returned `lines` array.
export async function createMagic({ onPrint, onPrintErr } = {}) {
  const lines = [];
  const mod = await createMagicModule({
    wasmBinary,
    print:    onPrint    ?? (msg => { lines.push(msg); console.log('[magic]', msg); }),
    printErr: onPrintErr ?? (msg => { lines.push(msg); console.error('[magic]', msg); }),
  });
  const magic = new MagicWasm(mod);
  magic.init();
  return { magic, lines };
}

// Loads a layout into the VFS. `tech` is the name of a technology that is
// either built into the WASM binary (e.g. 'scmos', 'minimum', 'nmos') or has
// already been written to /magic/sys/current/<tech>.tech by the caller.
export function loadCell(FS, tech, magFile) {
  const cell = basename(magFile, '.mag');
  vfsWrite(FS, `/work/${cell}.mag`, magFile);
  return { tech, cell };
}

// Reads a TCL script from EXAMPLES_DIR, substitutes __TECH__ and __CELL__.
export function loadScript(name, tech, cell) {
  return readFileSync(resolve(EXAMPLES_DIR, name), 'utf8')
    .replaceAll('__TECH__', tech)
    .replaceAll('__CELL__', cell);
}
