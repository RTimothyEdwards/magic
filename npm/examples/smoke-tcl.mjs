// Smoke-test that confirms the TCL interpreter is live inside magic.wasm.
//
// In wrapper mode magic_wasm_run_command routes its argument to
// Tcl_EvalEx(magicinterp, ...). So:
//   - pure Tcl (`set x 42; puts ...`) should work
//   - magic commands are available as ::magic:: ensemble commands too
//
// Run: node npm/examples/smoke-tcl.mjs

// Pull in the TCL-enabled variant explicitly via the /tcl subpath export.
import createMagic from '../tcl.js';

const m = await createMagic();

const status = m.init();
if (status !== 0) {
  console.error(`magic_wasm_init failed: ${status}`);
  process.exit(1);
}
console.log('magic_wasm_init: OK');

function runTcl(label, command) {
  const rc = m.runCommand(command);
  console.log(`[rc=${rc}] ${label}: ${command}`);
  return rc;
}

// 1. Pure Tcl arithmetic — proves the TCL interp is parsing.
runTcl('tcl-set',  'set tcl_smoke_x 42');
runTcl('tcl-expr', 'set tcl_smoke_y [expr {$tcl_smoke_x * 2}]');

// 2. Tcl introspection — magic should publish a Tclmagic package.
runTcl('tcl-info', 'puts "tcl_version=$tcl_version  patchlevel=$tcl_patchLevel"');
runTcl('tcl-pkgs', 'puts "packages=[package names]"');

// 3. A real magic command via the wrapper.
runTcl('magic-help', 'magic::help');

console.log('done');
