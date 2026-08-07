if(NOT MPC_BUILD_MPCCHAP)
  return()
endif()

find_path(CUEFILE_INCLUDE_DIR cuetools/cuefile.h)
find_library(CUEFILE_LIBRARY NAMES cuefile)

if(NOT CUEFILE_INCLUDE_DIR OR NOT CUEFILE_LIBRARY)
  message(STATUS "mpcchap skipped (libcuefile not found)")
  return()
endif()

add_executable(mpcchap
    mpcchap.c
    dictionary.c
    iniparser.c
    ${PROJECT_SOURCE_DIR}/common/tags.c)

# The project's own <mpc/*.h> headers must be searched before any
# system-installed copy of libmpcdec (e.g. Homebrew).
target_include_directories(mpcchap PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CUEFILE_INCLUDE_DIR}
    ${PROJECT_SOURCE_DIR}/libmpcdec
    ${PROJECT_SOURCE_DIR}/libmpcenc)
target_link_libraries(mpcchap PRIVATE mpcdec mpcenc_static ${CUEFILE_LIBRARY})

if(NOT MSVC)
  target_link_libraries(mpcchap PRIVATE m)
endif()

install(TARGETS mpcchap RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
