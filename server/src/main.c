/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause)
*/
/// \file main.c
/// `musicpack-server` CLI: scan / serve / verify.
///
///   musicpack-server scan    --library DIR [--database PATH] [--verify]
///   musicpack-server serve   --library DIR [--database PATH]
///                            [--listen IP] [--port N] [--no-scan]
///   musicpack-server verify  --library DIR [--database PATH]
///
/// Configuration precedence: command line > MUSICPACK_LIBRARY /
/// MUSICPACK_DATABASE / MUSICPACK_LISTEN / MUSICPACK_PORT > defaults.
/// Defaults bind to loopback only; remote access is never implied.
#include "config.h"
#include "http.h"
#include "library.h"
#include "log.h"
#include "scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>

static void
usage(void)
{
    fprintf(stderr,
        "usage: musicpack-server <command> [options]\n"
        "commands:\n"
        "  scan     index the library (deterministic, idempotent)\n"
        "  serve    scan (unless --no-scan) then serve the HTTP API\n"
        "  verify   scan with full integrity verification (sha256)\n"
        "  help     show this message\n"
        "  version  show version\n"
        "options:\n"
        "  --library DIR     music root (default ./library, env MUSICPACK_LIBRARY)\n"
        "  --database PATH   sqlite database (default ./library.db, env MUSICPACK_DATABASE)\n"
        "  --listen IP       bind address (default 127.0.0.1, env MUSICPACK_LISTEN)\n"
        "  --port N          listen port (default 8080, env MUSICPACK_PORT)\n"
        "  --verify          full sha256 integrity verification during scan\n"
        "  --no-scan         serve without a startup scan\n");
}

enum {
    OPT_LIBRARY = 1000, OPT_DATABASE, OPT_LISTEN, OPT_PORT,
    OPT_VERIFY, OPT_NO_SCAN, OPT_HELP, OPT_VERSION,
};

static const struct option long_opts[] = {
    { "library", required_argument, 0, OPT_LIBRARY },
    { "database", required_argument, 0, OPT_DATABASE },
    { "listen", required_argument, 0, OPT_LISTEN },
    { "port", required_argument, 0, OPT_PORT },
    { "verify", no_argument, 0, OPT_VERIFY },
    { "no-scan", no_argument, 0, OPT_NO_SCAN },
    { "help", no_argument, 0, OPT_HELP },
    { "version", no_argument, 0, OPT_VERSION },
    { 0, 0, 0, 0 },
};

static int
parse_options(int argc, char **argv, mp_config *cfg)
{
    int c, bad = 0;
    optind = 1;
    while ((c = getopt_long(argc, argv, "", long_opts, 0)) != -1) {
        switch (c) {
        case OPT_LIBRARY: mp_config_set_str(cfg->library, sizeof cfg->library, optarg); break;
        case OPT_DATABASE: mp_config_set_str(cfg->database, sizeof cfg->database, optarg); break;
        case OPT_LISTEN: mp_config_set_str(cfg->listen, sizeof cfg->listen, optarg); break;
        case OPT_PORT: cfg->port = atoi(optarg); break;
        case OPT_VERIFY: cfg->verify_on_scan = 1; break;
        case OPT_NO_SCAN: cfg->no_scan = 1; break;
        case OPT_HELP: usage(); exit(0);
        case OPT_VERSION:
            printf("musicpack-server %s\n", MUSICPACK_VERSION);
            exit(0);
        default: bad = 1; break;
        }
    }
    return bad ? -1 : optind;
}

static int
run_scan(const mp_config *cfg, int verify)
{
    mp_library *lib;
    mp_scan_result res;
    char err[256];

    lib = mp_library_open(cfg->database, 1, err, sizeof err);
    if (lib == 0) {
        fprintf(stderr, "musicpack-server: cannot open database: %s\n", err);
        return 1;
    }
    if (mp_scan_library(lib, cfg->library, verify, &res) != MUSICPACK_OK) {
        fprintf(stderr, "musicpack-server: scan failed\n");
        mp_library_close(lib);
        return 1;
    }
    printf("scan: %d packages (%d added, %d updated, %d moved, "
           "%d removed, %d invalid)\n",
           res.total, res.added, res.updated, res.moved, res.removed,
           res.invalid);
    mp_library_close(lib);
    return 0;
}

static int
file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f != 0) {
        fclose(f);
        return 1;
    }
    return 0;
}

static int
run_serve(const mp_config *cfg)
{
    mp_library *lib;
    char err[256];

    if (cfg->no_scan && !file_exists(cfg->database)) {
        fprintf(stderr,
                "musicpack-server: database '%s' does not exist; run "
                "`musicpack-server scan` first (or drop --no-scan)\n",
                cfg->database);
        return 1;
    }
    lib = mp_library_open(cfg->database, 1, err, sizeof err);
    if (lib == 0) {
        fprintf(stderr, "musicpack-server: cannot open database: %s\n", err);
        return 1;
    }
    if (!cfg->no_scan) {
        mp_scan_result res;
        MP_LOGI("startup scan");
        mp_scan_library(lib, cfg->library, cfg->verify_on_scan, &res);
    }
    if (mp_http_serve(lib, cfg, err, sizeof err) != 0) {
        fprintf(stderr, "musicpack-server: %s\n", err);
        mp_library_close(lib);
        return 1;
    }
    mp_library_close(lib);
    return 0;
}

int
main(int argc, char **argv)
{
    const char *cmd;
    mp_config cfg;
    int rest;

    mp_log_init("musicpack-server");
    if (argc < 2) {
        usage();
        return 2;
    }
    cmd = argv[1];
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage();
        return 0;
    }
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        printf("musicpack-server %s\n", MUSICPACK_VERSION);
        return 0;
    }
    if (strcmp(cmd, "scan") != 0 && strcmp(cmd, "serve") != 0 &&
        strcmp(cmd, "verify") != 0) {
        fprintf(stderr, "musicpack-server: unknown command '%s'\n", cmd);
        usage();
        return 2;
    }

    mp_config_defaults(&cfg);
    mp_config_apply_env(&cfg);
    rest = parse_options(argc - 1, argv + 1, &cfg);
    if (rest < 0) {
        usage();
        return 2;
    }
    if (cfg.port <= 0 || cfg.port > 65535) {
        fprintf(stderr, "musicpack-server: invalid port %d\n", cfg.port);
        return 2;
    }

    if (strcmp(cmd, "scan") == 0)
        return run_scan(&cfg, cfg.verify_on_scan);
    if (strcmp(cmd, "verify") == 0)
        return run_scan(&cfg, 1);
    return run_serve(&cfg);
}
