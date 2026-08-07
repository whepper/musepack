if(NOT MPC_BUILD_MPCGAIN)
  return()
endif()

find_path(REPLAY_GAIN_INCLUDE_DIR replaygain/gain_analysis.h
    /usr/include
    /usr/local/include)
find_library(REPLAY_GAIN_LIBRARY NAMES replaygain
    /usr/lib
    /usr/local/lib)

if(NOT REPLAY_GAIN_INCLUDE_DIR OR NOT REPLAY_GAIN_LIBRARY)
  message(STATUS "mpcgain skipped (libreplaygain not found)")
  return()
endif()

add_executable(mpcgain mpcgain.c)

# The project's own <mpc/*.h> headers must be searched before any
# system-installed copy of libmpcdec (e.g. Homebrew).
target_include_directories(mpcgain PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/libmpcdec
    ${REPLAY_GAIN_INCLUDE_DIR})
target_link_libraries(mpcgain PRIVATE mpcdec ${REPLAY_GAIN_LIBRARY})

if(NOT MSVC)
  target_link_libraries(mpcgain PRIVATE m)
endif()

install(TARGETS mpcgain RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
