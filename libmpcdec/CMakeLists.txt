set(MPCDEC_SOURCES
    huffman.c
    mpc_bits_reader.c
    mpc_decoder.c
    mpc_demux.c
    mpc_reader.c
    requant.c
    streaminfo.c
    synth_filter.c
    ${PROJECT_SOURCE_DIR}/common/crc32.c)

if(MPC_BUILD_SHARED)
  add_library(mpcdec SHARED ${MPCDEC_SOURCES})
else()
  add_library(mpcdec STATIC ${MPCDEC_SOURCES})
endif()

target_link_libraries(mpcdec PUBLIC mpc_headers)

# pthread_once is used to build the huffman LUTs exactly once.
find_package(Threads REQUIRED)
target_link_libraries(mpcdec PUBLIC Threads::Threads)

if(MPC_BUILD_SHARED)
  set_target_properties(mpcdec PROPERTIES
      VERSION ${PROJECT_VERSION}
      SOVERSION ${PROJECT_VERSION_MAJOR}
      OUTPUT_NAME mpcdec)
endif()

# libmpcdec:7:0:1 (libtool) == major 7
set_target_properties(mpcdec PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)

install(TARGETS mpcdec
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# pkg-config metadata
set(prefix ${CMAKE_INSTALL_PREFIX})
configure_file(libmpcdec.pc.in ${CMAKE_CURRENT_BINARY_DIR}/libmpcdec.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libmpcdec.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
