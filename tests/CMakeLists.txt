# Tests for the Musepack libraries and tools. Only included when
# MPC_BUILD_TESTS is ON (see the top-level CMakeLists.txt).

# ---- C unit tests (link against the real libraries) ------------------------
add_executable(mpc_unit_tests unit_tests.c)
target_link_libraries(mpc_unit_tests PRIVATE mpcdec mpcenc_static)
target_include_directories(mpc_unit_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/libmpcdec
    ${PROJECT_SOURCE_DIR}/libmpcenc)

add_test(NAME unit COMMAND mpc_unit_tests)
if(WIN32)
  # The shared libmpcdec DLL must be findable at runtime.
  set_tests_properties(unit PROPERTIES
      ENVIRONMENT "PATH=$<TARGET_FILE_DIR:mpcdec>$<SEMICOLON>$ENV{PATH}")
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

    # Bit-identity with the pristine reference encoder over the synthetic
    # corpus (tests/reference_manifest.txt, generated from upstream r475).
    add_test(NAME compat
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_compat.sh
                     $<TARGET_FILE:mpcenc>)
endif()
