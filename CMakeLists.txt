cmake_minimum_required(VERSION 3.16)

project(libmpc
        VERSION 7.0.1
        DESCRIPTION "Musepack (MPC) audio codec"
        HOMEPAGE_URL "https://www.musepack.net/"
        LANGUAGES C)

include(TestBigEndian)
test_big_endian(MPC_BIG_ENDIAN_HOST)

# ---- options ----------------------------------------------------------------
option(MPC_BUILD_SHARED "Build libmpcdec as a shared library" ON)
option(MPC_BUILD_TESTS   "Build and run the fixture regression tests" OFF)
option(MPC_BUILD_MPCGAIN "Build mpcgain (requires libreplaygain)" ON)
option(MPC_BUILD_MPCCHAP "Build mpcchap (requires libcuefile)" ON)

if(WIN32 AND NOT MSVC)
  set(MPC_BUILD_SHARED OFF)
endif()

# ---- global settings --------------------------------------------------------
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT MSVC)
  add_compile_options(-Wall -Wextra)
endif()

# 64-bit file offsets for POSIX builds (needed by common/fileio.h). Must be
# defined before any system header that pulls in off_t.
if(NOT WIN32)
  add_compile_definitions(_FILE_OFFSET_BITS=64)
endif()

# ---- subdirectories ---------------------------------------------------------
add_subdirectory(include)
add_subdirectory(libmpcdec)
add_subdirectory(libmpcenc)
add_subdirectory(libmpcpsy)
add_subdirectory(libwavformat)
add_subdirectory(mpcdec)
add_subdirectory(mpcenc)
add_subdirectory(mpc2sv8)
add_subdirectory(mpccut)
add_subdirectory(wavcmp)
add_subdirectory(mpcgain)
add_subdirectory(mpcchap)

if(MPC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
