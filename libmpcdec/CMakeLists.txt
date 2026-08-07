set(MPCDEC_SOURCES
    huffman.c
    mpc_bits_reader.c
    mpc_decoder.c
    mpc_demux.c
    mpc_reader.c
    musepack_decoder.c
    requant.c
    streaminfo.c
    synth_filter.c
    ${PROJECT_SOURCE_DIR}/common/crc32.c)

# ---------------------------------------------------------------------------
# Shared variant (default off Windows, on elsewhere). Its public symbols are
# exported via MUSEPACK_API/MPC_API; everything else is hidden with
# -fvisibility=hidden so internal tables and bit readers stay out of the ABI.
# ---------------------------------------------------------------------------
if(MPC_BUILD_SHARED)
  add_library(musepack SHARED ${MPCDEC_SOURCES})
  set_target_properties(musepack PROPERTIES
      VERSION ${PROJECT_VERSION}
      SOVERSION ${PROJECT_VERSION_MAJOR}
      OUTPUT_NAME musepack
      C_STANDARD 11 C_STANDARD_REQUIRED ON
      C_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON
      EXPORT_NAME Decoder)
  # dllexport for the new API; DLL_EXPORT drives the legacy MPC_API macro.
  target_compile_definitions(musepack PRIVATE MUSEPACK_BUILD_SHARED)
  if(WIN32)
    target_compile_definitions(musepack PRIVATE DLL_EXPORT)
  endif()
  # Consumers of the shared library import its symbols on Windows.
  target_compile_definitions(musepack PUBLIC MUSEPACK_USE_SHARED)
else()
  add_library(musepack STATIC ${MPCDEC_SOURCES})
  set_target_properties(musepack PROPERTIES
      OUTPUT_NAME musepack
      C_STANDARD 11 C_STANDARD_REQUIRED ON
      EXPORT_NAME Decoder)
endif()

# ---------------------------------------------------------------------------
# Static variant: produced alongside the shared library for in-tree
# white-box consumers (the unit test reaches into private headers). The SV8
# block tools only need legacy MPC_API bit readers, which stay exported, so
# they can link the shared library.
# ---------------------------------------------------------------------------
if(MPC_BUILD_SHARED)
  add_library(musepack_static STATIC ${MPCDEC_SOURCES})
  set_target_properties(musepack_static PROPERTIES
      OUTPUT_NAME musepack
      C_STANDARD 11 C_STANDARD_REQUIRED ON
      EXPORT_NAME Static)
endif()

# ---- common interface (headers, threads, libm) ---------------------------
find_package(Threads REQUIRED)
target_link_libraries(musepack PUBLIC mpc_headers Threads::Threads)
if(UNIX AND NOT APPLE)
  # libm provides pow/log10 etc. (macOS provides them in libSystem).
  target_link_libraries(musepack PUBLIC m)
endif()
if(MPC_BUILD_SHARED)
  target_link_libraries(musepack_static PUBLIC mpc_headers Threads::Threads)
  if(UNIX AND NOT APPLE)
    target_link_libraries(musepack_static PUBLIC m)
  endif()
endif()

# ---------------------------------------------------------------------------
# Install / package
# ---------------------------------------------------------------------------
if(MPC_BUILD_SHARED)
  install(TARGETS musepack musepack_static
      EXPORT musepack-targets
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  install(TARGETS musepack
      EXPORT musepack-targets
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# pkg-config metadata: the new identity plus the historical name.
set(prefix ${CMAKE_INSTALL_PREFIX})
configure_file(musepack.pc.in ${CMAKE_CURRENT_BINARY_DIR}/musepack.pc @ONLY)
configure_file(libmpcdec.pc.in ${CMAKE_CURRENT_BINARY_DIR}/libmpcdec.pc @ONLY)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/musepack.pc
    ${CMAKE_CURRENT_BINARY_DIR}/libmpcdec.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# CMake package config: find_package(Musepack CONFIG) -> Musepack::Decoder
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/MusepackConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MusepackConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/MusepackConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Musepack)
install(EXPORT musepack-targets
    FILE MusepackTargets.cmake
    NAMESPACE Musepack::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Musepack)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/MusepackConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/MusepackConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Musepack)
