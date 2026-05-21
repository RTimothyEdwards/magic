// TCL-enabled entry point: import from "magic-vlsi-wasm/tcl".
//
// In this variant magic.wasm embeds a full Tcl 9 interpreter (from
// tcltk/tcl, pinned via magic/npm/tcl.ref) and `runCommand(str)` calls
// Tcl_EvalEx(magicinterp, str, ...).  Pure Tcl works:
//
//     await magic.runCommand('set x 42');
//     await magic.runCommand('puts $tcl_version');
//
// Magic commands are exposed as the ::magic:: ensemble:
//
//     await magic.runCommand('magic::tech load sky130A');
//     await magic.runCommand('magic::load /work/inv');
//
// (Bare command names like "tech load …" are not imported into the global
// namespace by this build — invoke them with the ::magic:: prefix, or set
// up `namespace import ::magic::*` yourself after init().)

import MagicModuleFactory from './tcl/magic.js';

async function createMagic(options = {}) {
  const module = await MagicModuleFactory(options);

  const init       = module.cwrap('magic_wasm_init',        'number', []);
  const runCommand = module.cwrap('magic_wasm_run_command', 'number', ['string']);
  const sourceFile = module.cwrap('magic_wasm_source_file', 'number', ['string']);
  const update     = module.cwrap('magic_wasm_update',      null,     []);

  return { init, runCommand, sourceFile, update, FS: module.FS, variant: 'tcl' };
}

export { createMagic };
export default createMagic;
