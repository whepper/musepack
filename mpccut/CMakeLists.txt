set(MPCCUT_SOURCES mpccut.c)
set(MPCCUT_INCLUDES
    ${PROJECT_SOURCE_DIR}/libmpcdec
    ${PROJECT_SOURCE_DIR}/libmpcenc)

if(MSVC)
  list(APPEND MPCCUT_SOURCES ${PROJECT_SOURCE_DIR}/win32/attgetopt.c)
  list(APPEND MPCCUT_INCLUDES ${PROJECT_SOURCE_DIR}/win32)
endif()

add_executable(mpccut ${MPCCUT_SOURCES})
target_include_directories(mpccut PRIVATE ${MPCCUT_INCLUDES})
target_link_libraries(mpccut PRIVATE mpcdec mpcenc_static)

if(NOT MSVC)
  target_link_libraries(mpccut PRIVATE m)
endif()

install(TARGETS mpccut RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
