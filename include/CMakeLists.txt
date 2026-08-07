# Public headers. Exposed as an INTERFACE target so libraries and tools can
# link it and get the include path + install rules for free.
add_library(mpc_headers INTERFACE)
target_include_directories(mpc_headers
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

install(DIRECTORY mpc DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
