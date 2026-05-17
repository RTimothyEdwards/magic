// Explicit non-TCL entry point: import from "magic-vlsi-wasm/notcl".
// Identical to the default `magic-vlsi-wasm` import — exists so callers can
// be explicit about which variant they want.

export { createMagic } from './index.js';
export { default } from './index.js';
