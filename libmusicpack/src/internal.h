/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD-2-Clause; see the top-level headers for the full text.)
*/
/// \file internal.h
/// Internal declarations shared between libmusicpack translation units.
#ifndef MUSICPACK_INTERNAL_H_
#define MUSICPACK_INTERNAL_H_

#include <musicpack/musicpack.h>

#include "cJSON.h"

/* manifest.c */
musicpack_status musicpack_manifest_parse_tree(const cJSON *root, musicpack_manifest *m);
musicpack_status musicpack_manifest_write_with_original(const musicpack_manifest *m,
                                                        const cJSON *original,
                                                        char **json_out);

/* package.c */
int musicpack_report_error(musicpack_report *rep);

#endif /* MUSICPACK_INTERNAL_H_ */
