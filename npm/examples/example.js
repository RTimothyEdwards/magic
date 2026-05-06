// example.js — GDS → CIF format conversion using the Magic WASM module.
//
// Usage:
//   node examples/example.js [input.gds] [output.cif]
//
// Defaults:
//   input:  design.gds  (in current working directory)
//   output: design.cif  (in current working directory)

import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve, basename } from 'node:path';
import createMagicModule from '../magic.js';

const __dirname = dirname(fileURLToPath(import.meta.url));

const wasmBinary = readFileSync(resolve(__dirname, '../magic.wasm'));

const inputGds  = process.argv[2] ?? 'design.gds';
const outputCif = process.argv[3] ?? inputGds.replace(/\.gds$/i, '.cif');
const cellName  = basename(inputGds, '.gds');

const module = await createMagicModule({
  wasmBinary,
  print:    msg => console.log('[magic]', msg),
  printErr: msg => console.error('[magic]', msg),
});

module.FS.mkdirTree('/work');
module.FS.writeFile(`/work/${cellName}.gds`, readFileSync(inputGds));

module._magic_wasm_init();
module.cwrap('magic_wasm_run_command', 'number', ['string'])(`gds read /work/${cellName}`);
module.cwrap('magic_wasm_run_command', 'number', ['string'])(`load ${cellName}`);
module.cwrap('magic_wasm_run_command', 'number', ['string'])(`cif write /work/${cellName}`);

const cifBytes = module.FS.readFile(`/work/${cellName}.cif`);
writeFileSync(outputCif, cifBytes);

console.log(`Converted ${inputGds} → ${outputCif}`);
