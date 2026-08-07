# libmusicpack: MusicPack package semantics (.mpack manifest, album/track
# model, assets, checksums, BS.1770 loudness, directory storage). Depends on
# libmusepack (codec) for the Musepack reader handoff; the reverse must never
# happen.

set(MUSICPACK_VERSION "0.1.0")

set(MUSICPACK_SOURCES
    src/manifest.c
    src/package.c
    src/object.c
    src/checksum.c
    src/loudness.c
    src/path.c
    src/meta/tag.c
    src/meta/vorbis.c
    src/meta/flac.c
    src/meta/apev2.c
    src/meta/mapping.c
    src/meta/mb.c
    vendor/cjson/cJSON.c
    vendor/ebur128/ebur128.c)

if(MPC_BUILD_SHARED)
  add_library(musicpack SHARED ${MUSICPACK_SOURCES})
  set_target_properties(musicpack PROPERTIES
      VERSION ${MUSICPACK_VERSION}
      SOVERSION 0
      OUTPUT_NAME musicpack
      C_STANDARD 11 C_STANDARD_REQUIRED ON
      C_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON
      EXPORT_NAME Package)
  target_compile_definitions(musicpack PRIVATE MUSICPACK_BUILD_SHARED)
  if(WIN32)
    target_compile_definitions(musicpack PRIVATE DLL_EXPORT)
  endif()
  target_compile_definitions(musicpack PUBLIC MUSICPACK_USE_SHARED)
else()
  add_library(musicpack STATIC ${MUSICPACK_SOURCES})
  set_target_properties(musicpack PROPERTIES
      OUTPUT_NAME musicpack
      C_STANDARD 11 C_STANDARD_REQUIRED ON
      EXPORT_NAME Package)
endif()

target_include_directories(musicpack
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cjson
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ebur128)

# libebur128 needs a bundled sys/queue.h on platforms without one (Windows).
if(WIN32)
  target_include_directories(musicpack PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ebur128/queue)
endif()

# Bound JSON nesting for untrusted input.
target_compile_definitions(musicpack PRIVATE CJSON_NESTING_LIMIT=100)

# Vendored code is third-party: suppress warnings from it entirely.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cjson/cJSON.c
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ebur128/ebur128.c
    PROPERTIES COMPILE_OPTIONS "$<$<C_COMPILER_ID:MSVC>:/W0>;$<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>")

# libmusepack provides the reader type + Musepack handoff.
target_link_libraries(musicpack PUBLIC musepack)

if(UNIX AND NOT APPLE)
  target_link_libraries(musicpack PUBLIC m)
endif()

# ---------------------------------------------------------------------------
# Install / package
# ---------------------------------------------------------------------------
install(TARGETS musicpack
    EXPORT musicpack-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/musicpack DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# pkg-config
set(prefix ${CMAKE_INSTALL_PREFIX})
configure_file(musicpack.pc.in ${CMAKE_CURRENT_BINARY_DIR}/musicpack.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/musicpack.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# CMake package: find_package(MusicPack CONFIG) -> MusicPack::Package
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/MusicPackConfigVersion.cmake
    VERSION ${MUSICPACK_VERSION}
    COMPATIBILITY SameMajorVersion)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MusicPackConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/MusicPackConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MusicPack)
install(EXPORT musicpack-targets
    FILE MusicPackTargets.cmake
    NAMESPACE MusicPack::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MusicPack)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/MusicPackConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/MusicPackConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MusicPack)
