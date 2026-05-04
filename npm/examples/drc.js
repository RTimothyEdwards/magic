// drc.js — Run design rule checking and report violations.
//
// Usage:  node examples/drc.js [magFile [techFile]]
import { createMagic, loadCell, loadScript,
         DEFAULT_TECH, DEFAULT_MAG } from './helpers.js';
import { fileURLToPath } from 'node:url';

export async function run({ magFile = DEFAULT_MAG, techFile = DEFAULT_TECH } = {}) {
  const output = [];
  const { magic } = await createMagic({
    onPrint:    msg => { output.push(msg); console.log('[magic]', msg); },
    onPrintErr: msg => { output.push(msg); console.error('[magic]', msg); },
  });
  const { FS } = magic;
  const { tech, cell } = loadCell(FS, techFile, magFile);

  magic.runScript(loadScript('drc.tcl', tech, cell));

  // Magic prints "Total DRC errors found: N" at the end of drc listall.
  const summary = output.find(l => /Total DRC errors/i.test(l));
  const match   = summary?.match(/(\d+)/);
  const violations = match ? parseInt(match[1], 10) : null;

  return { violations, output };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const { violations } = await run().catch(e => { console.error(e.message ?? e); process.exit(1); });
  console.log(`\nDRC violations: ${violations ?? '(count not found in output)'}`);
  console.log('Done.');
}
