// all.js — Run all Magic WASM example tests and print a summary.
//
// Usage:  node examples/all.js
import { run as runExtract } from './extract.js';
import { run as runGds }     from './gds.js';
import { run as runDrc }     from './drc.js';
import { run as runCif }     from './cif.js';

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
    default:        return '';
  }
}

console.log('\nMagic WASM — test suite\n');

const suite = [
  ['extract', runExtract],
  ['gds',     runGds],
  ['drc',     runDrc],
  ['cif',     runCif],
];

const passed = [];
for (const [name, fn] of suite) {
  passed.push(await test(name, fn));
}

const ok = passed.filter(Boolean).length;
console.log(`\n${ok}/${suite.length} passed`);
process.exit(ok === suite.length ? 0 : 1);
