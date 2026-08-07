# Public headers. Exposed as an INTERFACE target so libraries and tools can
# link it and get the include path + install rules for free.
add_library(mpc_headers INTERFACE)
target_include_directories(mpc_headers
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

# Installed with libmusepack's export set so find_package(Musepack CONFIG)
# provides Musepack::Headers.
set_target_properties(mpc_headers PROPERTIES EXPORT_NAME Headers)

install(TARGETS mpc_headers EXPORT musepack-targets)
install(DIRECTORY mpc musepack DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
