// all-tcl.js — Run the full test suite against the TCL variant.
//
// Covers all non-TCL tests (extract, gds, drc, cif) run through the
// Tcl interpreter, plus a PCell generation test that is TCL-only.
//
// Usage:  node examples/all-tcl.js
import { run as runExtract } from './extract-tcl.js';
import { run as runGds }     from './gds-tcl.js';
import { run as runDrc }     from './drc-tcl.js';
import { run as runCif }     from './cif-tcl.js';
import { run as runPcell }   from './pcell.js';

const PAD = 9;

async function test(name, fn) {
  process.stdout.write(`  ${name.padEnd(PAD)} `);
  try {
    const result = await fn();
    console.log(`PASS  ${formatResult(name, result)}`);
    return true;
  } catch (e) {
    console.log(`FAIL  ${e.message ?? e}`);
    return false;
  }
}

function formatResult(name, r) {
  if (!r) return '';
  switch (name) {
    case 'extract': return [r.ext, r.spice].filter(Boolean).map(p => p.split(/[\\/]/).pop()).join(', ');
    case 'gds':     return `${r.outPath.split(/[\\/]/).pop()} (${r.bytes} B)`;
    case 'cif':     return `${r.outPath.split(/[\\/]/).pop()} (${r.bytes} B)`;
    case 'drc':     return r.violations != null ? `${r.violations} violation${r.violations !== 1 ? 's' : ''}` : '';
    case 'pcell':   return Object.entries(r).map(([n, { bytes }]) => `${n}.gds (${bytes} B)`).join(', ');
    default:        return '';
  }
}

console.log('\nMagic WASM TCL variant — test suite\n');

const suite = [
  ['extract', runExtract],
  ['gds',     runGds],
  ['drc',     runDrc],
  ['cif',     runCif],
  ['pcell',   runPcell],
];

const passed = [];
for (const [name, fn] of suite) {
  passed.push(await test(name, fn));
}

const ok = passed.filter(Boolean).length;
console.log(`\n${ok}/${suite.length} passed`);
process.exit(ok === suite.length ? 0 : 1);
