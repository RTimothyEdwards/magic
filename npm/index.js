import MagicModuleFactory from './magic.js';

async function createMagic(options = {}) {
  const module = await MagicModuleFactory(options);

  const init       = module.cwrap('magic_wasm_init',        'number', []);
  const runCommand = module.cwrap('magic_wasm_run_command',  'number', ['string']);
  const sourceFile = module.cwrap('magic_wasm_source_file',  'number', ['string']);
  const update     = module.cwrap('magic_wasm_update',       null,     []);

  return { init, runCommand, sourceFile, update, FS: module.FS };
}

export { createMagic };
export default createMagic;
