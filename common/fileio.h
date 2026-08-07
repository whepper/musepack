/*
 * Portable 64-bit file positioning.
 *
 * fseeko/ftello/off_t are POSIX-only and, on glibc, require feature-test
 * macros; MSVC has none of them. This header provides a single pair of
 * functions that work everywhere:
 *
 *   mpc_file_seek(FILE *, mpc_seek_t offset, int whence)
 *   mpc_file_tell(FILE *)
 *
 * They map to _fseeki64/_ftelli64 on Windows and fseeko/ftello elsewhere.
 */
#ifndef MPC_FILEIO_H
#define MPC_FILEIO_H

/*
 * 64-bit file positioning on POSIX requires off_t to be 64-bit wide, which
 * glibc only guarantees when _FILE_OFFSET_BITS=64 is in effect before any
 * system header is included. That define is applied project-wide via CMake
 * (see the top-level CMakeLists.txt); the guard here documents the contract
 * and ensures consistency for direct compiles.
 */
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
# define _FILE_OFFSET_BITS 64
#endif

#include <stdio.h>
#if !defined(_WIN32)
# include <sys/types.h>
#endif

#include <mpc/mpc_types.h>

#if defined(_WIN32)
# define mpc_file_seek(fp, offset, whence) _fseeki64((fp), (__int64) (offset), (whence))
# define mpc_file_tell(fp)                 ((mpc_seek_t) _ftelli64(fp))
#else
# define mpc_file_seek(fp, offset, whence) fseeko((fp), (off_t) (offset), (whence))
# define mpc_file_tell(fp)                 ((mpc_seek_t) ftello(fp))
#endif

#endif /* MPC_FILEIO_H */
