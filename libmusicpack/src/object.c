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
/// \file object.c
/// Audio object access and the Musepack reader handoff.

#include <string.h>

#include "internal.h"
#include <musicpack/object.h>
#include <musicpack/path.h>

static const musicpack_track *
find_track(const musicpack_package *pkg, size_t disc, size_t track)
{
    const musicpack_manifest *m = musicpack_package_manifest(pkg);
    if (m == 0 || disc >= m->disc_count || track >= m->discs[disc].track_count)
        return 0;
    return &m->discs[disc].tracks[track];
}

musicpack_status
musicpack_package_track_path(const musicpack_package *pkg, size_t disc, size_t track,
                             char *out, size_t cap)
{
    const musicpack_track *tr = find_track(pkg, disc, track);
    if (tr == 0)
        return MUSICPACK_ERR_INVALID;
    return musicpack_package_resolve_path(pkg, tr->audio.path, out, cap);
}

musicpack_status
musicpack_package_track_open_reader(const musicpack_package *pkg, size_t disc,
                                    size_t track, mpc_reader *reader)
{
    const musicpack_track *tr = find_track(pkg, disc, track);
    char abs[MUSICPACK_PATH_MAX + 2];
    musicpack_status s;

    if (tr == 0 || reader == 0)
        return MUSICPACK_ERR_INVALID;
    s = musicpack_package_resolve_path(pkg, tr->audio.path, abs, sizeof abs);
    if (s != MUSICPACK_OK)
        return s;
    if (mpc_reader_init_stdio(reader, abs) != MPC_STATUS_OK)
        return MUSICPACK_ERR_IO;
    return MUSICPACK_OK;
}
