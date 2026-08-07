/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

  * Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the following
  disclaimer in the documentation and/or other materials provided
  with the distribution.

  * Neither the name of the The MusicPack Development Team nor the
  names of its contributors may be used to endorse or promote
  products derived from this software without specific prior
  written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/// \file export.h
/// Export/import macros and API version for the libmusicpack public API.
#ifndef MUSICPACK_EXPORT_H_
#define MUSICPACK_EXPORT_H_

#if defined(MUSICPACK_BUILD_SHARED)
#  if defined(_WIN32)
#    define MUSICPACK_API __declspec(dllexport)
#  else
#    define MUSICPACK_API __attribute__((visibility("default")))
#  endif
#elif defined(MUSICPACK_USE_SHARED)
#  if defined(_WIN32)
#    define MUSICPACK_API __declspec(dllimport)
#  else
#    define MUSICPACK_API
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define MUSICPACK_API __attribute__((visibility("default")))
#else
#  define MUSICPACK_API
#endif

#define MUSICPACK_VERSION_MAJOR 0
#define MUSICPACK_VERSION_MINOR 1
#define MUSICPACK_VERSION_PATCH 0
#define MUSICPACK_VERSION "0.1.0"

/// Incremented only when the public API changes incompatibly.
#define MUSICPACK_API_VERSION 1

#endif /* MUSICPACK_EXPORT_H_ */
