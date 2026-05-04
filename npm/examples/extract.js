// extract.js — RC extraction example (extract → extresist → ext2spice).
//
// Usage:  node examples/extract.js [magFile [techFile [outputDir]]]
import { createMagic, vfsWrite, vfsRead, loadCell, loadScript,
         DEFAULT_TECH, DEFAULT_MAG, DEFAULT_OUT } from './helpers.js';
import { writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

export async function run({ magFile = DEFAULT_MAG, techFile = DEFAULT_TECH, outputDir = DEFAULT_OUT } = {}) {
  const { magic } = await createMagic();
  const { FS } = magic;
  const { tech, cell } = loadCell(FS, techFile, magFile);

  magic.runScript(loadScript('extract.tcl', tech, cell));

  mkdirSync(outputDir, { recursive: true });

  const extData = vfsRead(FS, `/work/${cell}.ext`);
  if (!extData) throw new Error(`Extraction failed: /work/${cell}.ext not created`);
  writeFileSync(resolve(outputDir, `${cell}.ext`), extData);

  const resExtData = vfsRead(FS, `/work/${cell}.res.ext`);
  if (resExtData) writeFileSync(resolve(outputDir, `${cell}.res.ext`), resExtData);

  const spiceData = vfsRead(FS, `/work/${cell}.spice`) ?? vfsRead(FS, `/work/${cell}.spc`);
  const spiceExt  = vfsRead(FS, `/work/${cell}.spice`) ? 'spice' : 'spc';
  if (spiceData) writeFileSync(resolve(outputDir, `${cell}.${spiceExt}`), spiceData);

  return {
    ext:   resolve(outputDir, `${cell}.ext`),
    spice: spiceData ? resolve(outputDir, `${cell}.${spiceExt}`) : null,
  };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const { ext, spice } = await run().catch(e => { console.error(e.message ?? e); process.exit(1); });
  console.log(`\next:   ${ext}`);
  if (spice) console.log(`spice: ${spice}`);
  console.log('Done.');
}
