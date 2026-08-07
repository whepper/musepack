set(MUSICPACK_TOOL_SOURCES main.c)

if(MSVC)
  list(APPEND MUSICPACK_TOOL_SOURCES ${PROJECT_SOURCE_DIR}/win32/attgetopt.c)
endif()

add_executable(musicpack_cmd ${MUSICPACK_TOOL_SOURCES})
if(MSVC)
  target_include_directories(musicpack_cmd PRIVATE ${PROJECT_SOURCE_DIR}/win32)
endif()
target_link_libraries(musicpack_cmd PRIVATE musicpack musepack)

# Historical-ish binary name "musicpack" (target is musicpack_cmd to avoid
# clashing with the musicpack library target).
set_target_properties(musicpack_cmd PROPERTIES OUTPUT_NAME musicpack)

if(NOT MSVC)
  target_link_libraries(musicpack_cmd PRIVATE m)
endif()

install(TARGETS musicpack_cmd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
