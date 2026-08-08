/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see http.h)
*/
#include "http.h"
#include "api.h"
#include "jobs.h"
#include "json.h"
#include "log.h"
#include "mime.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <microhttpd.h>

static volatile sig_atomic_t g_stop = 0;

/* Small JSON error response for the static handler (api.c has its own). */
static struct MHD_Response *
static_error(unsigned int status, const char *code, const char *message)
{
    char *body = mp_json_error(code, message);
    struct MHD_Response *r =
        MHD_create_response_from_buffer(strlen(body), (void *) body,
                                        MHD_RESPMEM_MUST_COPY);
    free(body);
    if (r != 0)
        MHD_add_response_header(r, MHD_HTTP_HEADER_CONTENT_TYPE,
                                "application/json; charset=utf-8");
    (void) status;
    return r;
}

static void
on_signal(int sig)
{
    (void) sig;
    g_stop = 1;
}

/* Creates and binds the listening socket ourselves so the bind address is
   under full control (loopback by default; never an accidental wildcard).
   MHD takes ownership of the returned fd via MHD_OPTION_LISTEN_SOCKET. */
static int
make_listen_socket(const char *ip, int port)
{
    struct sockaddr_in addr;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;

    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t) port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    if (bind(fd, (struct sockaddr *) &addr, sizeof addr) != 0 ||
        listen(fd, 32) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Serves the reference demo static files (gated by --static-dir) with the
   cross-origin isolation headers the SharedArrayBuffer reader requires.
   Paths are resolved through musicpack_path_resolve (containment, no ".."),
   so this can never escape the configured directory. */
static struct MHD_Response *
serve_static(const mp_config *cfg, const char *url, const char *method,
             unsigned int *status_out)
{
    char rel[MUSICPACK_PATH_MAX + 2];
    char abs[MUSICPACK_PATH_MAX + 2];
    const char *p = url;
    int fd;
    struct stat st;
    struct MHD_Response *resp;

    if (*p == '/')
        p++;
    if (*p == '\0')
        snprintf(rel, sizeof rel, "index.html");
    else
        snprintf(rel, sizeof rel, "%s", p);
    if (musicpack_path_resolve(cfg->static_dir, rel, abs, sizeof abs)
        != MUSICPACK_OK) {
        *status_out = 404;
        return static_error(404, "not_found", "Not found");
    }
    fd = open(abs, O_RDONLY | O_NOFOLLOW);
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        if (fd >= 0)
            close(fd);
        *status_out = 404;
        return static_error(404, "not_found", "Not found");
    }
    resp = MHD_create_response_from_fd64((uint64_t) st.st_size, fd);
    if (resp == 0) {
        close(fd);
        *status_out = 500;
        return static_error(500, "internal", "cannot create response");
    }
    MHD_add_response_header(resp, "Content-Type", mp_mime_for_path(rel));
    MHD_add_response_header(resp, "Cache-Control", "no-cache");
    /* Cross-origin isolation for the SharedArrayBuffer range reader. */
    MHD_add_response_header(resp, "Cross-Origin-Opener-Policy", "same-origin");
    MHD_add_response_header(resp, "Cross-Origin-Embedder-Policy",
                            "require-corp");
    (void) method;
    *status_out = 200;
    return resp;
}

static enum MHD_Result
access_handler(void *cls, struct MHD_Connection *c, const char *url,
               const char *method, const char *version,
               const char *upload_data, size_t *upload_data_size, void **con_cls)
{
    mp_server_ctx *srv = (mp_server_ctx *) cls;
    struct MHD_Response *response;
    unsigned int status;

    (void) version;
    (void) upload_data;
    (void) upload_data_size;
    (void) con_cls;

    if (srv->cfg->static_dir[0] != '\0' &&
        strncmp(url, "/api/", 5) != 0) {
        response = serve_static(srv->cfg, url, method, &status);
    } else {
        response = mp_api_handle(srv, c, method, url, &status);
    }
    if (response == 0) {
        MP_LOGE("no response for %s %s", method, url);
        return MHD_NO;
    }
    {
        enum MHD_Result rc = MHD_queue_response(c, status, response);
        MHD_destroy_response(response);
        return rc;
    }
}

int
mp_http_serve(mp_library *lib, const mp_config *cfg, mp_job_state *jobs,
              char *err, size_t errcap)
{
    struct MHD_Daemon *daemon;
    struct sigaction sa;
    int listen_fd;
    mp_server_ctx srv;
    struct MHD_OptionItem opts[] = {
        { MHD_OPTION_LISTEN_SOCKET, 0, 0 },
        { MHD_OPTION_CONNECTION_LIMIT, 256, 0 },
        { MHD_OPTION_PER_IP_CONNECTION_LIMIT, 64, 0 },
        { MHD_OPTION_CONNECTION_TIMEOUT, 60, 0 },
        { MHD_OPTION_END, 0, 0 },
    };

    listen_fd = make_listen_socket(cfg->listen, cfg->port);
    if (listen_fd < 0) {
        if (err != 0 && errcap > 0)
            snprintf(err, errcap, "cannot bind %s:%d", cfg->listen, cfg->port);
        return -1;
    }
    opts[0].value = listen_fd;

    srv.lib = lib;
    srv.cfg = cfg;
    srv.jobs = jobs;

    g_stop = 0;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGPIPE, &sa, 0); /* MHD uses MSG_NOSIGNAL; be safe anyway */

    daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_POLL,
        0, 0, 0, access_handler, (void *) &srv,
        MHD_OPTION_ARRAY, opts, MHD_OPTION_END);
    if (daemon == 0) {
        close(listen_fd);
        if (err != 0 && errcap > 0)
            snprintf(err, errcap, "cannot start HTTP server on %s:%d",
                     cfg->listen, cfg->port);
        return -1;
    }
    MP_LOGI("serving http://%s:%d (library=%s, database=%s, static=%s)",
            cfg->listen, cfg->port, cfg->library, cfg->database,
            cfg->static_dir[0] != '\0' ? cfg->static_dir : "-");
    while (!g_stop)
        sleep(1);
    MHD_stop_daemon(daemon);
    mp_jobs_wait(jobs);
    MP_LOGI("server stopped");
    return 0;
}
