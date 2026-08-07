# Legacy build systems

These build systems are no longer maintained and are kept only for reference.
The supported build system is **CMake** (see the top-level `CMakeLists.txt`).

## autotools

The autotools build (`configure.in`, `Makefile.am` files, `Makefile.cvs`) was
removed because it could no longer be regenerated: `configure.in` referenced
`config/` and `m4/` directories that are not present and called an undefined
`CHECK_VISIBILITY` macro, and the deprecated `AM_CONFIG_HEADER` /
`AC_PROG_LIBTOOL` macros were removed from modern automake/libtool.

To resurrect it, the file must be updated to current autoconf/automake
practices (rename to `configure.ac`, use `AC_INIT` with proper arguments,
`AM_INIT_AUTOMAKE` with explicit options, `LT_INIT` for libtool, and define
`CHECK_VISIBILITY`).

## vstudio2005

Visual Studio 2005-era `.vcproj` project files and `.sln` solutions. These use
the obsolete `.vcproj` XML format that modern MSVC no longer supports; use the
CMake build on Windows instead.
