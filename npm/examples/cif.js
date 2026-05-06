// cif.js — Export layout to CIF (Caltech Intermediate Form).
//
// Usage:  node examples/cif.js [magFile [tech [outputDir]]]
import { createMagic, vfsRead, loadCell, loadScript,
         DEFAULT_TECH, DEFAULT_MAG, DEFAULT_OUT } from './helpers.js';
import { writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

export async function run({ magFile = DEFAULT_MAG, tech = DEFAULT_TECH, outputDir = DEFAULT_OUT } = {}) {
  const { magic } = await createMagic();
  const { FS } = magic;
  const { tech: techName, cell } = loadCell(FS, tech, magFile);

  magic.runScript(loadScript('cif.tcl', techName, cell));

  mkdirSync(outputDir, { recursive: true });
  const data = vfsRead(FS, `/work/${cell}.cif`);
  if (!data) throw new Error(`CIF export failed: /work/${cell}.cif not created`);

  const outPath = resolve(outputDir, `${cell}.cif`);
  writeFileSync(outPath, data);
  return { outPath, bytes: data.length };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const { outPath, bytes } = await run().catch(e => { console.error(e.message ?? e); process.exit(1); });
  console.log(`\ncif: ${outPath} (${bytes} bytes)`);
  console.log('Done.');
}
