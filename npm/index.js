// Default entry point: the non-TCL build.
//
// This preserves the original API and behavior of magic-vlsi-wasm. Magic
// commands ("tech load sky130A", "load /work/inv", …) are dispatched through
// magic's legacy parser; no Tcl interpreter is involved.
//
// For the TCL-enabled build (commands are evaluated by Tcl_EvalEx, exposing
// the full Tcl 9 runtime alongside the ::magic:: command ensemble), import
// from "magic-vlsi-wasm/tcl" instead.

import MagicModuleFactory from './notcl/magic.js';

async function createMagic(options = {}) {
  const module = await MagicModuleFactory(options);

  const init       = module.cwrap('magic_wasm_init',        'number', []);
  const runCommand = module.cwrap('magic_wasm_run_command', 'number', ['string']);
  const sourceFile = module.cwrap('magic_wasm_source_file', 'number', ['string']);
  const update     = module.cwrap('magic_wasm_update',      null,     []);

  return { init, runCommand, sourceFile, update, FS: module.FS, variant: 'notcl' };
}

export { createMagic };
export default createMagic;
