# Public headers. Exposed as an INTERFACE target so libraries and tools can
# link it and get the include path + install rules for free.
add_library(mpc_headers INTERFACE)

# Generated version header (values from the top-level project()).
configure_file(musepack/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/musepack/version.h @ONLY)

target_include_directories(mpc_headers
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>)

# Installed with libmusepack's export set so find_package(Musepack CONFIG)
# provides Musepack::Headers.
set_target_properties(mpc_headers PROPERTIES EXPORT_NAME Headers)

install(TARGETS mpc_headers EXPORT musepack-targets)
install(DIRECTORY mpc musepack DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/musepack/version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/musepack)
