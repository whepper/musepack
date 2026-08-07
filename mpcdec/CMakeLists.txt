set(MPCDEC_TOOL_SOURCES mpcdec.c)
set(MPCDEC_TOOL_INCLUDES
    ${PROJECT_SOURCE_DIR}/libmpcdec
    ${PROJECT_SOURCE_DIR}/libwavformat)

if(MSVC)
  list(APPEND MPCDEC_TOOL_SOURCES ${PROJECT_SOURCE_DIR}/win32/attgetopt.c)
  list(APPEND MPCDEC_TOOL_INCLUDES ${PROJECT_SOURCE_DIR}/win32)
endif()

add_executable(mpcdec_cmd ${MPCDEC_TOOL_SOURCES})
target_include_directories(mpcdec_cmd PRIVATE ${MPCDEC_TOOL_INCLUDES})
target_link_libraries(mpcdec_cmd PRIVATE mpcdec wavformat)

# Historical binary name is "mpcdec" (target is mpcdec_cmd to avoid clashing
# with the mpcdec library target).
set_target_properties(mpcdec_cmd PROPERTIES OUTPUT_NAME mpcdec)

if(NOT MSVC)
  target_link_libraries(mpcdec_cmd PRIVATE m)
endif()

install(TARGETS mpcdec_cmd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
