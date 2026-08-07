/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see config.h)
*/
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
mp_config_defaults(mp_config *c)
{
    if (c == 0)
        return;
    memset(c, 0, sizeof *c);
    snprintf(c->library, sizeof c->library, "./library");
    snprintf(c->database, sizeof c->database, "./library.db");
    snprintf(c->listen, sizeof c->listen, "127.0.0.1");
    c->port = 8080;
    c->verify_on_scan = 0;
    c->no_scan = 0;
}

void
mp_config_apply_env(mp_config *c)
{
    const char *v;
    if (c == 0)
        return;
    if ((v = getenv("MUSICPACK_LIBRARY")) != 0 && *v != '\0')
        mp_config_set_str(c->library, sizeof c->library, v);
    if ((v = getenv("MUSICPACK_DATABASE")) != 0 && *v != '\0')
        mp_config_set_str(c->database, sizeof c->database, v);
    if ((v = getenv("MUSICPACK_LISTEN")) != 0 && *v != '\0')
        mp_config_set_str(c->listen, sizeof c->listen, v);
    if ((v = getenv("MUSICPACK_PORT")) != 0 && *v != '\0')
        c->port = atoi(v);
}

void
mp_config_set_str(char *dst, size_t cap, const char *value)
{
    if (dst == 0 || cap == 0 || value == 0)
        return;
    snprintf(dst, cap, "%s", value);
}
