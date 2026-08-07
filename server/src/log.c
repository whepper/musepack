/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see log.h)
*/
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_prog = "musicpack-server";
static int g_level = MP_LOG_INFO;

void
mp_log_init(const char *prog)
{
    if (prog != 0)
        g_prog = prog;
    const char *env = getenv("MUSICPACK_LOG");
    if (env != 0 && strcmp(env, "debug") == 0)
        g_level = MP_LOG_DEBUG;
    else if (env != 0 && strcmp(env, "error") == 0)
        g_level = MP_LOG_ERROR;
    else if (env != 0 && strcmp(env, "warn") == 0)
        g_level = MP_LOG_WARN;
}

void
mp_log_set_level(int level)
{
    g_level = level;
}

void
mp_logf(int level, const char *fmt, ...)
{
    static const char *tag[] = { "error", "warn", "info", "debug" };
    va_list ap;

    if (level > g_level || (unsigned) level > 3)
        return;
    fprintf(stderr, "%s[%s]: ", g_prog, tag[level]);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}
