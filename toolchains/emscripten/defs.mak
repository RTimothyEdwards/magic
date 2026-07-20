# WASM-specific make additions — append to the configure-generated defs.mak.
#
# Usage (matches what the CI workflow does):
#
#   source <emsdk>/emsdk_env.sh
#   CFLAGS="--std=c17 -D_DEFAULT_SOURCE=1 -DEMSCRIPTEN=1 -g" \
#   LDFLAGS="-gsource-maps" \
#     emconfigure ./configure --without-cairo --without-opengl --without-x \
#                             --without-tk   --without-tcl  \
#                             --disable-readline --disable-compression \
#                             --target=asmjs-unknown-emscripten
#   cat toolchains/emscripten/defs.mak >> defs.mak
#   emmake make depend
#   emmake make -j$(nproc) modules libs
#   emmake make techs
#   emmake make mains

# Activate the WASM link target in magic/Makefile.
MAKE_WASM = 1

# Preserve DWARF debug info through emcc -r (partial-link) steps.
# Without -g, emcc -r defaults to GENERATE_DWARF=0, which causes building.py
# to add --strip-debug to wasm-ld, discarding the DWARF produced by -g in
# CFLAGS. That leaves magic.wasm without debug sections, making -gsource-map
# produce an empty source map at the final link.
LINK = $(LD) -r $(LDFLAGS)

# Emscripten linker flags.
# The link step runs from the magic/ subdirectory, so embed-file paths
# are relative to that directory (../scmos, ../windows/...).
#
# INCOMING_MODULE_JS_API is emscripten's default list plus `wasmBinary`.
# Our JS loaders (npm/examples/*.js) pass Module.wasmBinary to embed the .wasm,
# but emscripten 6.0.2 dropped wasmBinary (and a batch of GL/SDL members) from
# the default INCOMING_MODULE_JS_API. With ASSERTIONS=1 that turns into a hard
# runtime abort: "`Module.wasmBinary` was supplied but `wasmBinary` not included
# in INCOMING_MODULE_JS_API". We spell out the full default list (so external
# consumers passing locateFile/arguments/etc. keep working) and re-add
# wasmBinary. Keep this in sync with emscripten's default if it grows.
TOP_EXTRA_LIBS += \
    ${TOP_FIRST_LIBS_WASM} \
    -sWASM=1 \
    -sMODULARIZE=1 \
    -sEXPORT_ES6=1 \
    -sUSE_ZLIB=1 \
    -sEXPORTED_FUNCTIONS=_magic_wasm_init,_magic_wasm_run_command,_magic_wasm_source_file,_magic_wasm_update \
    -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,FS,setValue,getValue \
    -sINCOMING_MODULE_JS_API=ENVIRONMENT,arguments,canvas,dynamicLibraries,elementPointerLock,instantiateWasm,locateFile,monitorRunDependencies,noExitRuntime,noInitialRun,onAbort,onExit,onRuntimeInitialized,postRun,preInit,preRun,print,printErr,setStatus,statusMessage,stderr,stdin,stdout,thisProgram,wasm,websocket,wasmBinary \
    -sALLOW_MEMORY_GROWTH=1 \
    -sINITIAL_MEMORY=67108864 \
    -Wl,-z,stack-size=10485760 \
    -sENVIRONMENT=node,web,worker \
    -sFORCE_FILESYSTEM=1 \
    ${TOP_EXTRA_LIBS_WASM} \
    --embed-file ../scmos@/magic/sys/current \
    --embed-file ../windows/windows7.glyphs@/magic/sys/windows7.glyphs \
    --embed-file ../windows/windows7.glyphs@/magic/sys/bw.glyphs
