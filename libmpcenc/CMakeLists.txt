add_library(mpcenc_static STATIC
    analy_filter.c
    bitstream.c
    encode_sv7.c
    huffsv7.c
    quant.c
    ${PROJECT_SOURCE_DIR}/common/crc32.c)

target_link_libraries(mpcenc_static PUBLIC mpc_headers)

install(TARGETS mpcenc_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
