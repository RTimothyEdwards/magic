// pcell.js — PCell generation test (TCL variant only).
//
// Defines a Tcl proc as a PCell, instantiates it with two different sizes,
// and verifies that both GDS outputs are non-empty.
//
// Usage:  node examples/pcell.js
import { createMagic, vfsRead, loadScript, DEFAULT_TECH, DEFAULT_OUT } from './helpers-tcl.js';
import { writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

export async function run({ tech = DEFAULT_TECH, outputDir = DEFAULT_OUT } = {}) {
  const { magic } = await createMagic();
  const { FS } = magic;

  FS.mkdirTree('/work');
  magic.runTcl(loadScript('pcell.tcl', tech, ''));

  mkdirSync(outputDir, { recursive: true });

  const cells = ['pcell_4x8', 'pcell_8x4'];
  const results = {};
  for (const name of cells) {
    const data = vfsRead(FS, `/work/${name}.gds`);
    if (!data || data.length === 0)
      throw new Error(`PCell GDS output missing or empty: /work/${name}.gds`);
    const outPath = resolve(outputDir, `${name}.gds`);
    writeFileSync(outPath, data);
    results[name] = { outPath, bytes: data.length };
  }
  return results;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const results = await run().catch(e => { console.error(e.message ?? e); process.exit(1); });
  for (const [name, { outPath, bytes }] of Object.entries(results))
    console.log(`  ${name}.gds: ${outPath} (${bytes} bytes)`);
  console.log('Done.');
}
