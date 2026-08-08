# musicpack-server: self-hosted MusicPack library server (Phase 4).
#
# Two layers:
#   musicpack_server (static core) - platform-independent: SQLite library
#     database, scanner, package identity, range parsing, MIME/codec probing.
#     Depends only on libmusicpack (+ vendored SQLite/cJSON). Built always.
#   musicpack_server_cmd (output "musicpack-server") - adds the HTTP API via
#     GNU libmicrohttpd (LGPL-2.1+, dynamically linked). Built only when MHD
#     is available; Windows server support is explicitly out of Phase 4 scope.

option(MPC_BUILD_SERVER "Build the musicpack-server executable (needs libmicrohttpd)" ON)

set(MUSICPACK_SERVER_SOURCES
    src/log.c
    src/db.c
    src/schema.c
    src/identity.c
    src/mime.c
    src/codec.c
    src/library.c
    src/scanner.c
    src/range.c
    src/json.c
    src/config.c
    src/random.c
    src/tokens.c
    src/sessions.c
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/sqlite3.c)

add_library(musicpack_server STATIC ${MUSICPACK_SERVER_SOURCES})
set_target_properties(musicpack_server PROPERTIES
    OUTPUT_NAME musicpack_server
    C_STANDARD 11 C_STANDARD_REQUIRED ON)

target_include_directories(musicpack_server
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite)

if(WIN32)
  target_include_directories(musicpack_server PRIVATE
      ${PROJECT_SOURCE_DIR}/win32)
  target_sources(musicpack_server PRIVATE ${PROJECT_SOURCE_DIR}/win32/dirent.c)
  target_link_libraries(musicpack_server PRIVATE bcrypt)
endif()

target_compile_definitions(musicpack_server PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_FOREIGN_KEYS=1)

target_link_libraries(musicpack_server PUBLIC musicpack)

# Vendored SQLite: compile without warnings.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/sqlite3.c
    PROPERTIES COMPILE_OPTIONS "$<$<C_COMPILER_ID:MSVC>:/W0>;$<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>")

if(UNIX AND NOT APPLE)
  target_link_libraries(musicpack_server PUBLIC m)
endif()

# ---- HTTP layer + CLI (requires libmicrohttpd) -----------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(MICROHTTPD QUIET IMPORTED_TARGET libmicrohttpd)
endif()

if(MPC_BUILD_SERVER)
  if(MICROHTTPD_FOUND)
    add_executable(musicpack_server_cmd src/main.c src/http.c src/api.c src/jobs.c)
    target_link_libraries(musicpack_server_cmd PRIVATE
        musicpack_server PkgConfig::MICROHTTPD)
    set_target_properties(musicpack_server_cmd PROPERTIES
        OUTPUT_NAME musicpack-server
        C_STANDARD 11 C_STANDARD_REQUIRED ON)
    if(NOT MSVC)
      target_link_libraries(musicpack_server_cmd PRIVATE m)
    endif()
    install(TARGETS musicpack_server_cmd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  else()
    if(NOT WIN32)
      message(WARNING
          "libmicrohttpd not found; building musicpack-server core only "
          "(install libmicrohttpd-dev to enable the server executable)")
    endif()
  endif()
endif()
