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
/// \file http.h
/// HTTP serving loop (libmicrohttpd).
///
/// Runs the daemon on a single internal polling thread: request handlers
/// execute on that thread, so the server needs no locks (SQLite is used from
/// one thread only; scan runs as a separate process over WAL). Concurrent
/// streams are multiplexed non-blocking by MHD.
#ifndef MPSERVER_HTTP_H_
#define MPSERVER_HTTP_H_

#include <stddef.h>

#include "config.h"
#include "jobs.h"
#include "library.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Starts the server and blocks until SIGINT/SIGTERM.
///
/// \param lib  opened library (read/write)
/// \param cfg  resolved configuration
/// \param jobs job state (scan/verify workers)
/// \param err  optional error buffer
/// \return 0 on clean shutdown, -1 on failure
int mp_http_serve(mp_library *lib, const mp_config *cfg, mp_job_state *jobs,
                  char *err, size_t errcap);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_HTTP_H_ */
