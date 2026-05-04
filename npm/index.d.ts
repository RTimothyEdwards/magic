export interface MagicInstance {
  /**
   * Initialize Magic (idempotent — safe to call multiple times).
   * Returns 0 on success, non-zero on failure.
   */
  init(): number;

  /**
   * Dispatch a single Magic command string.
   * Calls init() automatically on the first invocation.
   * Returns 0 on success, non-zero on failure.
   */
  runCommand(command: string): number;

  /**
   * Read and execute a command file at the given virtual filesystem path.
   * Calls init() automatically on the first invocation.
   * Returns 0 on success, -1 if the file could not be opened.
   */
  sourceFile(path: string): number;

  /**
   * Drive a display-update cycle.
   * No-op in headless builds — the null display driver suspends all redraws.
   */
  update(): void;

  /**
   * Emscripten virtual filesystem.
   * Use FS.writeFile / FS.readFile to pass layout files in and out of Magic.
   * See https://emscripten.org/docs/api_reference/Filesystem-API.html
   */
  FS: any;
}

/**
 * Create a Magic WASM instance.
 *
 * @param options  Emscripten module options. Useful keys:
 *   - `wasmBinary`  Pre-fetched ArrayBuffer of magic.wasm (avoids a second fetch)
 *   - `print`       Callback for stdout lines  (default: console.log)
 *   - `printErr`    Callback for stderr lines  (default: console.error)
 *
 * @example
 * ```js
 * import createMagic from 'magic-vlsi-wasm';
 *
 * const { runCommand, FS } = await createMagic();
 * FS.writeFile('/work/inv.mag', layoutBytes);
 * runCommand('tech load sky130A');
 * runCommand('load /work/inv');
 * runCommand('gds write /work/inv');
 * const gds = FS.readFile('/work/inv.gds');
 * ```
 */
export default function createMagic(options?: Record<string, unknown>): Promise<MagicInstance>;
export { createMagic };
