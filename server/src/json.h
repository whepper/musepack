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
/// \file json.h
/// Minimal JSON writer for API responses (the server emits only; it never
/// parses untrusted JSON). String values are validated UTF-8 by
/// libmusicpack upstream, so the escaper passes non-ASCII bytes through.
#ifndef MPSERVER_JSON_H_
#define MPSERVER_JSON_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mp_json mp_json;

/* constructors */
mp_json *mp_json_obj(void);
mp_json *mp_json_arr(void);
void mp_json_free(mp_json *j);

/* attach a child (obj: key != NULL; arr: key == NULL) */
void mp_json_add(mp_json *parent, const char *key, mp_json *child);

/* convenience: add scalar members to an object */
void mp_json_str(mp_json *o, const char *key, const char *value);
void mp_json_str_opt(mp_json *o, const char *key, const char *value); /* omit NULL/empty */
void mp_json_int(mp_json *o, const char *key, long long value);
void mp_json_dbl(mp_json *o, const char *key, double value);

/* scalar leaf nodes for arrays */
mp_json *mp_json_strnode(const char *value);
mp_json *mp_json_intnode(long long value);

/* renders compact JSON; caller frees */
char *mp_json_render(mp_json *j);

/* error envelope {error:{code,message}}; caller frees */
char *mp_json_error(const char *code, const char *message);

#ifdef __cplusplus
}
#endif
#endif /* MPSERVER_JSON_H_ */
