/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see mime.h)
*/
#include "mime.h"

#include <string.h>

static const char *
ext_of(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot != 0 ? dot : "";
}

const char *
mp_mime_for_path(const char *path)
{
    const char *e = ext_of(path);
    if (strcmp(e, ".mpc") == 0)  return "audio/musepack";
    if (strcmp(e, ".flac") == 0) return "audio/flac";
    if (strcmp(e, ".wav") == 0)  return "audio/wav";
    if (strcmp(e, ".ogg") == 0)  return "audio/ogg";
    if (strcmp(e, ".jpg") == 0 || strcmp(e, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(e, ".png") == 0)  return "image/png";
    if (strcmp(e, ".gif") == 0)  return "image/gif";
    if (strcmp(e, ".webp") == 0) return "image/webp";
    if (strcmp(e, ".bmp") == 0)  return "image/bmp";
    if (strcmp(e, ".pdf") == 0)  return "application/pdf";
    if (strcmp(e, ".html") == 0 || strcmp(e, ".htm") == 0) return "text/html";
    if (strcmp(e, ".js") == 0 || strcmp(e, ".mjs") == 0) return "text/javascript";
    if (strcmp(e, ".css") == 0)  return "text/css";
    if (strcmp(e, ".json") == 0) return "application/json";
    if (strcmp(e, ".svg") == 0)  return "image/svg+xml";
    if (strcmp(e, ".woff") == 0) return "font/woff";
    if (strcmp(e, ".woff2") == 0) return "font/woff2";
    if (strcmp(e, ".ico") == 0)  return "image/x-icon";
    if (strcmp(e, ".wasm") == 0) return "application/wasm";
    if (strcmp(e, ".map") == 0)  return "application/json";
    if (strcmp(e, ".lrc") == 0 || strcmp(e, ".txt") == 0 ||
        strcmp(e, ".md") == 0)   return "text/plain";
    return "application/octet-stream";
}

const char *
mp_codec_for_path(const char *path)
{
    const char *e = ext_of(path);
    if (strcmp(e, ".mpc") == 0)  return "musepack";
    if (strcmp(e, ".flac") == 0) return "flac";
    if (strcmp(e, ".wav") == 0)  return "wav";
    if (strcmp(e, ".ogg") == 0)  return "vorbis";
    return "unknown";
}
