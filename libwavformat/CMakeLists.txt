add_library(wavformat STATIC
    input.c
    output.c)

target_include_directories(wavformat PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)

install(TARGETS wavformat ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
