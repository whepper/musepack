# WebAssembly build of the libmusepack decoder wrapper.
#
# This is only included when building with the Emscripten toolchain
# (emcmake cmake -S . -B build-wasm). It compiles the same decoder core
# (the `musepack` static library) plus this thin wrapper into a single
# browser/node-usable module: musepack.js + musepack.wasm.

add_executable(musepack_wasm musepack_wasm.c)
target_link_libraries(musepack_wasm PRIVATE musepack)

set_target_properties(musepack_wasm PROPERTIES OUTPUT_NAME musepack)

# Single-threaded, no SharedArrayBuffer, no experimental extensions.
# ASYNCIFY lets the JS range reader (mpc_wasm_open_range, backed by the
# declared imports in range_library.js) block on HTTP Range fetches, so
# decoder reads/seeks go over the wire.
target_link_options(musepack_wasm PRIVATE
    "-O3"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFILESYSTEM=0"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createMusepackModule"
    "-sASYNCIFY=1"
    "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/range_library.js"
    "-sEXPORTED_FUNCTIONS=_mpc_wasm_create,_mpc_wasm_open,_mpc_wasm_open_range,_mpc_wasm_sample_rate,_mpc_wasm_channels,_mpc_wasm_stream_version,_mpc_wasm_length_samples,_mpc_wasm_position,_mpc_wasm_read,_mpc_wasm_seek_sample,_mpc_wasm_seek_seconds,_mpc_wasm_check,_mpc_wasm_destroy,_malloc,_free"
    "-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32"
    "-sENVIRONMENT=web,worker,node")
