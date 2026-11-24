/* UDP sockets on 127.0.0.1: opening, addressing and waiting with a timeout (issue #6). */
#include "udp.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct sockaddr_in udp_addr(uint16_t port)
{
    struct sockaddr_in addr;

    // zero everything first: sin_zero (and any field we do not set) must be 0
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return addr;
}

int udp_open(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;

    /*
        bound to 127.0.0.1, not INADDR_ANY, so the socket is not reachable from outside this machine.
        no SO_REUSEADDR: UDP has no TIME_WAIT, a port is free again as soon as its socket is closed.
     */
    struct sockaddr_in addr = udp_addr(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int saved = errno; // close() may change errno
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

uint16_t udp_local_port(int fd)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    if (getsockname(fd, (struct sockaddr *)&addr, &len) < 0)
        return 0;
    return ntohs(addr.sin_port);
}

int udp_wait(int fd, int timeout_ms)
{
    struct pollfd p = { .fd = fd, .events = POLLIN };

    for (;;) {
        int r = poll(&p, 1, timeout_ms);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            return 0;
        // poll() does not fail on a closed descriptor: it reports POLLNVAL in revents instead
        if (p.revents & POLLNVAL) {
            errno = EBADF;
            return -1;
        }
        return 1;
    }
}
