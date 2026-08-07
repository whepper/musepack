add_executable(mpcenc
    keyboard.c
    mpcenc.c
    pipeopen.c
    stderr.c
    wave_in.c
    winmsg.c
    ${PROJECT_SOURCE_DIR}/common/tags.c)

target_include_directories(mpcenc PRIVATE
    ${PROJECT_SOURCE_DIR}/libmpcpsy
    ${PROJECT_SOURCE_DIR}/libmpcenc
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mpcenc PRIVATE mpcpsy mpcenc_static)

# Encoder uses the optimized math tables and must match libmpcpsy's FAST_MATH.
target_compile_definitions(mpcenc PRIVATE FAST_MATH)

if(MPC_BIG_ENDIAN_HOST)
  target_compile_definitions(mpcenc PRIVATE MPC_BIG_ENDIAN)
endif()

if(WIN32)
  target_link_libraries(mpcenc PRIVATE winmm)
endif()

if(NOT MSVC)
  target_link_libraries(mpcenc PRIVATE m)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
  target_link_libraries(mpcenc PRIVATE ossaudio)
endif()

install(TARGETS mpcenc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
