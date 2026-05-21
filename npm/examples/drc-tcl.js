// drc-tcl.js — DRC check via the TCL variant.
import { createMagic, loadCell, loadScript,
         DEFAULT_TECH, DEFAULT_MAG } from './helpers-tcl.js';
import { fileURLToPath } from 'node:url';

export async function run({ magFile = DEFAULT_MAG, tech = DEFAULT_TECH } = {}) {
  const output = [];
  const { magic } = await createMagic({
    onPrint:    msg => { output.push(msg); console.log('[magic-tcl]', msg); },
    onPrintErr: msg => { output.push(msg); console.error('[magic-tcl]', msg); },
  });
  const { FS } = magic;
  const { tech: techName, cell } = loadCell(FS, tech, magFile);

  magic.runScript(loadScript('drc-magic.tcl', techName, cell));

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
