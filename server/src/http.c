/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see http.h)
*/
#include "http.h"
#include "api.h"
#include "log.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <microhttpd.h>

static volatile sig_atomic_t g_stop = 0;

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

static enum MHD_Result
access_handler(void *cls, struct MHD_Connection *c, const char *url,
               const char *method, const char *version,
               const char *upload_data, size_t *upload_data_size, void **con_cls)
{
    mp_library *lib = (mp_library *) cls;
    struct MHD_Response *response;
    unsigned int status;

    (void) version;
    (void) upload_data;
    (void) upload_data_size;
    (void) con_cls;
    response = mp_api_handle(lib, c, method, url, &status);
    if (response == 0)
        return MHD_NO;
    {
        enum MHD_Result rc = MHD_queue_response(c, status, response);
        MHD_destroy_response(response);
        return rc;
    }
}

int
mp_http_serve(mp_library *lib, const mp_config *cfg, char *err, size_t errcap)
{
    struct MHD_Daemon *daemon;
    struct sigaction sa;
    int listen_fd;
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

    g_stop = 0;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGPIPE, &sa, 0); /* MHD uses MSG_NOSIGNAL; be safe anyway */

    daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_POLL,
        0, 0, 0, access_handler, (void *) lib,
        MHD_OPTION_ARRAY, opts, MHD_OPTION_END);
    if (daemon == 0) {
        close(listen_fd);
        if (err != 0 && errcap > 0)
            snprintf(err, errcap, "cannot start HTTP server on %s:%d",
                     cfg->listen, cfg->port);
        return -1;
    }
    MP_LOGI("serving http://%s:%d (library=%s, database=%s)",
            cfg->listen, cfg->port, cfg->library, cfg->database);
    while (!g_stop)
        sleep(1);
    MHD_stop_daemon(daemon);
    MP_LOGI("server stopped");
    return 0;
}
