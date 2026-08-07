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
/// \file config.h
/// Server configuration (Phase 4: minimal, explicit).
///
/// Precedence: command line > environment > defaults. Defaults are safe:
/// loopback-only binding, no remote access implied.
#ifndef MPSERVER_CONFIG_H_
#define MPSERVER_CONFIG_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mp_config {
    char library[4096];
    char database[4096];
    char listen[128];
    int port;
    int verify_on_scan;   ///< scan/verify: full integrity verification
    int no_scan;          ///< serve: skip the startup scan
} mp_config;

/// Fills \p c with defaults (library=./library, database=./library.db,
/// listen=127.0.0.1, port=8080).
void mp_config_defaults(mp_config *c);

/// Overrides any default with MUSICPACK_LIBRARY / MUSICPACK_DATABASE /
/// MUSICPACK_LISTEN / MUSICPACK_PORT when set.
void mp_config_apply_env(mp_config *c);

/// Copies a value into a config string field (truncated to the buffer).
void mp_config_set_str(char *dst, size_t cap, const char *value);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_CONFIG_H_ */
