# Tests for the Musepack libraries and tools. Only included when
# MPC_BUILD_TESTS is ON (see the top-level CMakeLists.txt).

# The WebAssembly build is decoder-only (no encoder/tools), so the only
# applicable test is the WASM smoke test.
if(EMSCRIPTEN)
  add_test(NAME wasm_smoke
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_wasm_smoke.sh
                   ${CMAKE_BINARY_DIR})
  return()
endif()

# ---- C unit tests (link against the real libraries) ------------------------
# The unit suite is white-box: it reaches into private decoder headers, so it
# links the static variant when a shared library is being built.
if(MPC_BUILD_SHARED)
  set(MPC_WHITEBOX_LIB musepack_static)
else()
  set(MPC_WHITEBOX_LIB musepack)
endif()

add_executable(mpc_unit_tests unit_tests.c)
target_link_libraries(mpc_unit_tests PRIVATE ${MPC_WHITEBOX_LIB} mpcenc_static)
target_include_directories(mpc_unit_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/libmpcdec
    ${PROJECT_SOURCE_DIR}/libmpcenc)

add_test(NAME unit COMMAND mpc_unit_tests)
if(WIN32)
  # The shared libmpcdec DLL must be findable at runtime.
  set_tests_properties(unit PROPERTIES
      ENVIRONMENT "PATH=$<TARGET_FILE_DIR:musepack>$<SEMICOLON>$ENV{PATH}")
endif()

# ---- stable libmusepack API tests -----------------------------------------
add_executable(mpc_api_tests api_tests.c)
target_link_libraries(mpc_api_tests PRIVATE musepack)
target_include_directories(mpc_api_tests PRIVATE ${PROJECT_SOURCE_DIR}/libmpcdec)

add_test(NAME api COMMAND mpc_api_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sine44-q5.mpc
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sine48-q6.mpc)
if(WIN32)
  set_tests_properties(api PROPERTIES
      ENVIRONMENT "PATH=$<TARGET_FILE_DIR:musepack>$<SEMICOLON>$ENV{PATH}")
endif()

# ---- .mpack / libmusicpack tests ------------------------------------------
add_executable(mpc_mpack_tests mpack_tests.c)
target_link_libraries(mpc_mpack_tests PRIVATE musicpack musepack)
if(UNIX AND NOT APPLE)
  target_link_libraries(mpc_mpack_tests PRIVATE m)
endif()
target_include_directories(mpc_mpack_tests PRIVATE ${PROJECT_SOURCE_DIR}/libmusicpack)

add_test(NAME mpack COMMAND mpc_mpack_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/reference/test-musicpack-album.mpack
    ${CMAKE_CURRENT_SOURCE_DIR}/reference/test-flac-album.mpack
    ${CMAKE_CURRENT_SOURCE_DIR}/reference/meta)
if(WIN32)
  set_tests_properties(mpack PROPERTIES
      ENVIRONMENT "PATH=$<TARGET_FILE_DIR:musepack>$<SEMICOLON>$<TARGET_FILE_DIR:musicpack>$<SEMICOLON>$ENV{PATH}")
endif()

# ---- shell-based integration / robustness tests (UNIX only) ---------------
if(UNIX)
    # Fixture regression: decode(fixture.mpc) must equal the golden .wav.
    add_test(NAME fixtures
             COMMAND ${CMAKE_COMMAND} -E env
                     MPCDEC=$<TARGET_FILE:mpcdec_cmd>
                     ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh)

    # End-to-end toolchain integration.
    add_test(NAME integration
             COMMAND ${CMAKE_COMMAND} -E env
                     ${CMAKE_CURRENT_SOURCE_DIR}/run_integration.sh
                     $<TARGET_FILE:mpcenc>
                     $<TARGET_FILE:mpcdec_cmd>
                     $<TARGET_FILE:mpccut>
                     $<TARGET_FILE:wavcmp>)

    # Decoder robustness on truncated/corrupted input.
    set(_fixture ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sine44-q5.mpc)
    add_test(NAME fuzz
             COMMAND ${CMAKE_COMMAND} -E env
                     ${CMAKE_CURRENT_SOURCE_DIR}/run_fuzz.sh
                     $<TARGET_FILE:mpcdec_cmd>
                     ${_fixture})

    # .mpack integration: reference packages, negative cases, JSON schema.
    add_test(NAME mpack_integration
             COMMAND ${CMAKE_COMMAND} -E env
                     ${CMAKE_CURRENT_SOURCE_DIR}/run_mpack.sh
                     $<TARGET_FILE:musicpack_cmd>
                     $<TARGET_FILE:mpc_mpack_tests>)

    # .mpack fuzz-lite: truncated/flipped manifests and path injection.
    add_test(NAME mpack_fuzz
             COMMAND ${CMAKE_COMMAND} -E env
                     ${CMAKE_CURRENT_SOURCE_DIR}/run_mpack_fuzz.sh
                     $<TARGET_FILE:musicpack_cmd>
                     ${CMAKE_CURRENT_SOURCE_DIR}/reference/test-musicpack-album.mpack)

    # Phase 3A meta fuzz-lite: truncated/flipped FLAC metadata files parsed
    # through the libmusicpack tag readers (no crash = pass).
    add_test(NAME meta_fuzz
             COMMAND ${CMAKE_COMMAND} -E env
                     ${CMAKE_CURRENT_SOURCE_DIR}/run_meta_fuzz.sh
                     $<TARGET_FILE:mpc_mpack_tests>
                     ${CMAKE_CURRENT_SOURCE_DIR}/reference/meta)
endif()

# ---- Bit-identity with the pristine reference encoder ----------------------
# Encodes the deterministic corpus and compares SHA-256 against either a
# reference encoder passed via REF_MPCENC (live comparison, used by CI so the
# check is toolchain-agnostic) or the committed -O0 manifest
# (tests/reference_manifest.txt). Runs on all platforms; Windows invokes the
# script through Git Bash.
if(UNIX)
    add_test(NAME compat
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_compat.sh
                     $<TARGET_FILE:mpcenc>)
elseif(WIN32)
    find_program(MPC_GIT_BASH bash
                 HINTS "C:/Program Files/Git/bin" "C:/Program Files/Git/usr/bin")
    if(MPC_GIT_BASH)
        add_test(NAME compat
                 COMMAND ${MPC_GIT_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/run_compat.sh
                         $<TARGET_FILE:mpcenc>)
    endif()
endif()
