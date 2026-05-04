// gds.js — Export layout to GDS II stream format.
//
// Usage:  node examples/gds.js [magFile [techFile [outputDir]]]
import { createMagic, vfsRead, loadCell, loadScript,
         DEFAULT_TECH, DEFAULT_MAG, DEFAULT_OUT } from './helpers.js';
import { writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

export async function run({ magFile = DEFAULT_MAG, techFile = DEFAULT_TECH, outputDir = DEFAULT_OUT } = {}) {
  const { magic } = await createMagic();
  const { FS } = magic;
  const { tech, cell } = loadCell(FS, techFile, magFile);

  magic.runScript(loadScript('gds.tcl', tech, cell));

  mkdirSync(outputDir, { recursive: true });
  const data = vfsRead(FS, `/work/${cell}.gds`);
  if (!data) throw new Error(`GDS export failed: /work/${cell}.gds not created`);

  const outPath = resolve(outputDir, `${cell}.gds`);
  writeFileSync(outPath, data);
  return { outPath, bytes: data.length };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const { outPath, bytes } = await run().catch(e => { console.error(e.message ?? e); process.exit(1); });
  console.log(`\ngds: ${outPath} (${bytes} bytes)`);
  console.log('Done.');
}
