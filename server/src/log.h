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
/// \file log.h
/// Minimal structured logging to stderr.
///
/// One line per event with a fixed tag prefix, e.g.
/// `musicpack-server[scan]: 3 packages added`. No logging of every served
/// range chunk (see AGENTS: keep logs useful, not noisy).
#ifndef MPSERVER_LOG_H_
#define MPSERVER_LOG_H_

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MP_LOG_ERROR = 0, ///< failures
    MP_LOG_WARN  = 1, ///< non-fatal anomalies
    MP_LOG_INFO  = 2, ///< startup / scan milestones
    MP_LOG_DEBUG = 3, ///< per-request detail (off by default)
};

/// Initializes logging (no-op). \p prog is used in the tag prefix.
void mp_log_init(const char *prog);

/// Sets the verbosity ceiling (default MP_LOG_INFO). MUSICPACK_LOG=debug
/// raises it.
void mp_log_set_level(int level);

/// Emits a tagged log line (fmt-style printf). Cheap; called per event.
void mp_logf(int level, const char *fmt, ...);

#define MP_LOGE(...) mp_logf(MP_LOG_ERROR, __VA_ARGS__)
#define MP_LOGW(...) mp_logf(MP_LOG_WARN, __VA_ARGS__)
#define MP_LOGI(...) mp_logf(MP_LOG_INFO, __VA_ARGS__)
#define MP_LOGD(...) mp_logf(MP_LOG_DEBUG, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_LOG_H_ */
