add_executable(wavcmp wavcmp.c)

target_include_directories(wavcmp PRIVATE ${PROJECT_SOURCE_DIR}/libwavformat)
target_link_libraries(wavcmp PRIVATE wavformat)

install(TARGETS wavcmp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
