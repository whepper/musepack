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

if(MPC_BUILD_SHARED)
  add_library(musepack SHARED ${MPCDEC_SOURCES})
else()
  add_library(musepack STATIC ${MPCDEC_SOURCES})
endif()

# Historical in-tree name; tools still link against it.
add_library(mpcdec ALIAS musepack)

target_link_libraries(musepack PUBLIC mpc_headers)

# pthread_once is used to build the huffman LUTs exactly once.
find_package(Threads REQUIRED)
target_link_libraries(musepack PUBLIC Threads::Threads)

# libm provides pow/log10 etc. On Linux this must be an explicit dependency
# of the shared library (macOS provides them in libSystem).
if(UNIX AND NOT APPLE)
  target_link_libraries(musepack PUBLIC m)
endif()

if(MPC_BUILD_SHARED)
  set_target_properties(musepack PROPERTIES
      VERSION ${PROJECT_VERSION}
      SOVERSION ${PROJECT_VERSION_MAJOR}
      OUTPUT_NAME musepack)
else()
  set_target_properties(musepack PROPERTIES OUTPUT_NAME musepack)
endif()

set_target_properties(musepack PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    EXPORT_NAME Decoder)

install(TARGETS musepack
    EXPORT musepack-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

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
