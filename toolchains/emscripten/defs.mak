# WASM-specific make additions — append to the configure-generated defs.mak.
#
# Usage (matches what the CI workflow does):
#
#   source <emsdk>/emsdk_env.sh
#   CFLAGS="--std=c17 -D_DEFAULT_SOURCE=1 -DEMSCRIPTEN=1 -g" \
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

# Emscripten linker flags.
# The link step runs from the magic/ subdirectory, so embed-file paths
# are relative to that directory (../scmos, ../windows/...).
TOP_EXTRA_LIBS += \
    -sWASM=1 \
    -sMODULARIZE=1 \
    -sEXPORT_ES6=1 \
    -sEXPORTED_FUNCTIONS=_magic_wasm_init,_magic_wasm_run_command,_magic_wasm_source_file,_magic_wasm_update \
    -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,FS,setValue,getValue \
    -sALLOW_MEMORY_GROWTH=1 \
    -sINITIAL_MEMORY=33554432 \
    -sSTACK_SIZE=5242880 \
    -sASSERTIONS=1 \
    -sENVIRONMENT=node,web,worker \
    -sFORCE_FILESYSTEM=1 \
    --embed-file ../scmos@/magic/sys/current \
    --embed-file ../windows/windows7.glyphs@/magic/sys/windows7.glyphs \
    --embed-file ../windows/windows7.glyphs@/magic/sys/bw.glyphs
