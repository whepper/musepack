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
/// \file jobs.h
/// Single scan/verify worker (Phase 5 live library maintenance).
///
/// Exactly one background job runs at a time (scan or verify), on its own
/// SQLite WAL connection, so the HTTP serving thread never blocks on
/// filesystem work and readers keep seeing the last committed state. The
/// MHD thread only ever takes a mutex-protected snapshot for /library/status.
#ifndef MPSERVER_JOBS_H_
#define MPSERVER_JOBS_H_

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MP_JOB_NONE = 0,
    MP_JOB_SCAN = 1,
    MP_JOB_VERIFY = 2,
};

typedef struct mp_job_state {
    int running;             ///< a job is currently running
    int kind;                ///< MP_JOB_SCAN / MP_JOB_VERIFY while running
    int last_kind;           ///< kind of the most recent completed job
    char started_at[32];
    char finished_at[32];
    /* scan counters */
    int packages_scanned;
    int added, updated, removed, invalid;
    /* verify counters */
    int verified_total, verified_passed, verified_warnings, verified_failed;
} mp_job_state;

/// Initializes the job state.
void mp_jobs_init(mp_job_state *st);

/// Starts a scan or verify job on a background thread. Returns 0 on success,
/// -1 if another job is already running (HTTP 409).
int mp_jobs_start(mp_job_state *st, const mp_config *cfg, int kind);

/// Waits for a running job to finish (blocks). Used at shutdown.
void mp_jobs_wait(mp_job_state *st);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_JOBS_H_ */
