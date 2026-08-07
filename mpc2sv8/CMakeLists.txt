set(MPC2SV8_SOURCES mpc2sv8.c)
set(MPC2SV8_INCLUDES
    ${PROJECT_SOURCE_DIR}/libmpcdec
    ${PROJECT_SOURCE_DIR}/libmpcenc)

if(MSVC)
  list(APPEND MPC2SV8_SOURCES
      ${PROJECT_SOURCE_DIR}/win32/attgetopt.c
      ${PROJECT_SOURCE_DIR}/win32/basename.c
      ${PROJECT_SOURCE_DIR}/win32/dirent.c)
  list(APPEND MPC2SV8_INCLUDES ${PROJECT_SOURCE_DIR}/win32)
endif()

add_executable(mpc2sv8 ${MPC2SV8_SOURCES})
target_include_directories(mpc2sv8 PRIVATE ${MPC2SV8_INCLUDES})
target_link_libraries(mpc2sv8 PRIVATE musepack mpcenc_static)

if(WIN32)
  target_link_libraries(mpc2sv8 PRIVATE winmm)
endif()

if(NOT MSVC)
  target_link_libraries(mpc2sv8 PRIVATE m)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
  target_link_libraries(mpc2sv8 PRIVATE ossaudio)
endif()

install(TARGETS mpc2sv8 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
