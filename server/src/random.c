/*
  Copyright (c) 2026, The MusicPack Development Team
  All rights reserved.
  (BSD 3-clause, see random.h)
*/
#include "random.h"

#include <string.h>

#ifdef _WIN32
# include <windows.h>
# include <bcrypt.h>
# pragma comment(lib, "bcrypt.lib")
#else
# include <errno.h>
# include <fcntl.h>
# include <stdlib.h> /* arc4random_buf on macOS/BSD */
# include <unistd.h>
# if defined(__linux__)
#  include <sys/random.h>
# endif
#endif

int
mp_random_bytes(unsigned char *out, size_t n)
{
    if (out == 0 || n == 0)
        return 0;
#ifdef _WIN32
    {
        NTSTATUS st = BCryptGenRandom(NULL, out, (ULONG) n,
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        return st == 0 ? 0 : -1;
    }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)
    arc4random_buf(out, n);
    return 0;
#else
    /* Linux and other POSIX systems. */
# if defined(__linux__)
    {
        size_t done = 0;
        while (done < n) {
            ssize_t r = getrandom(out + done, n - done, 0);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            done += (size_t) r;
        }
        if (done == n)
            return 0;
        /* fall through to /dev/urandom */
    }
# endif
    {
        int fd = open("/dev/urandom", O_RDONLY);
        size_t done = 0;
        if (fd < 0)
            return -1;
        while (done < n) {
            ssize_t r = read(fd, out + done, n - done);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                close(fd);
                return -1;
            }
            if (r == 0) {
                close(fd);
                return -1;
            }
            done += (size_t) r;
        }
        close(fd);
        return 0;
    }
#endif
}
