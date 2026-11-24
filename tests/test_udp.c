/*
    unit tests for the UDP helpers (issue #6).
    every socket is opened with port 0, so the OS picks free ports and the test never
    collides with another program (or another test run) on a fixed port.
    exit status 0 when every check passes, 1 otherwise.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "packet.h"
#include "udp.h"

static int failures = 0;

/*
    prints the result of one check and counts it if it failed.
 */
static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

/* milliseconds on the monotonic clock, only to measure how long udp_wait() blocked */
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* sends len bytes from fd to 127.0.0.1:port; returns what sendto() returned */
static ssize_t send_to_port(int fd, const void *data, size_t len, uint16_t port)
{
    struct sockaddr_in dst = udp_addr(port);
    return sendto(fd, data, len, 0, (struct sockaddr *)&dst, sizeof(dst));
}

/* recv() that gives up after 1 s, so a missing datagram fails a check instead of hanging the test */
static ssize_t recv_within(int fd, uint8_t *buffer, size_t cap)
{
    if (udp_wait(fd, 1000) != 1)
        return -1;
    return recv(fd, buffer, cap, 0);
}

/*
    1. udp_addr: IPv4, and port and address in network byte order on the wire.
 */
static void test_addr(void)
{
    struct sockaddr_in a = udp_addr(9001);
    const uint8_t *port = (const uint8_t *)&a.sin_port;
    const uint8_t *ip = (const uint8_t *)&a.sin_addr.s_addr;

    check(a.sin_family == AF_INET, "udp_addr is an IPv4 address");
    check(port[0] == 0x23 && port[1] == 0x29, "port 9001 is stored as bytes 23 29 (big-endian)");
    check(ip[0] == 127 && ip[1] == 0 && ip[2] == 0 && ip[3] == 1, "address is 127.0.0.1");
}

/*
    2. udp_open: port 0 gets a free port on 127.0.0.1, and a port that is already
    in use is refused with EADDRINUSE (the receiver then exits with status 2).
 */
static void test_open(void)
{
    int fd = udp_open(0);
    check(fd >= 0, "udp_open(0) opens a socket");
    if (fd < 0)
        return;

    uint16_t port = udp_local_port(fd);
    check(port != 0, "the OS chose a port");

    struct sockaddr_in local;
    socklen_t len = sizeof(local);
    getsockname(fd, (struct sockaddr *)&local, &len);
    check(local.sin_addr.s_addr == udp_addr(0).sin_addr.s_addr, "the socket is bound to 127.0.0.1, not to every interface");

    errno = 0;
    int again = udp_open(port);
    check(again == -1 && errno == EADDRINUSE, "a port already in use: -1 with errno EADDRINUSE");
    if (again >= 0)
        close(again);

    close(fd);
    check(udp_local_port(fd) == 0, "udp_local_port of a closed descriptor is 0");
}

/*
    3. one exchange as in the real programs: the "sender" sends a DATA packet, the
    "receiver" answers to the address recvfrom() gave it, and the ACK arrives back.
 */
static void test_round_trip(void)
{
    static uint8_t out[MAX_PKT], in[MAX_PKT];
    uint8_t payload[100];
    memset(payload, 0xAB, sizeof(payload));

    int snd = udp_open(0), rcv = udp_open(0);
    check(snd >= 0 && rcv >= 0, "two sockets open");
    if (snd < 0 || rcv < 0)
        return;

    size_t n = packet_build(out, TYPE_DATA, FLAG_LAST, 5, payload, sizeof(payload));
    check(send_to_port(snd, out, n, udp_local_port(rcv)) == (ssize_t)n, "sendto sends the whole DATA packet");

    check(udp_wait(rcv, 1000) == 1, "udp_wait reports the waiting datagram");

    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t got = recvfrom(rcv, in, sizeof(in), 0, (struct sockaddr *)&from, &from_len);
    check(got == (ssize_t)n && memcmp(in, out, n) == 0, "recvfrom returns the same bytes");
    check(got >= 0 && packet_check(in, (size_t)got, TYPE_DATA, 7) == PKT_OK, "the received DATA packet passes packet_check");
    check(ntohs(from.sin_port) == udp_local_port(snd), "recvfrom gives the sender's port as source");

    // the receiver never needs to know the sender's port: it answers the source address
    n = packet_build(out, TYPE_ACK, 0, 5, NULL, 0);
    check(sendto(rcv, out, n, 0, (struct sockaddr *)&from, from_len) == (ssize_t)n, "the ACK is sent to that source address");

    check(udp_wait(snd, 1000) == 1, "the ACK arrives at the sender");
    got = recv(snd, in, sizeof(in), 0);
    check(got == (ssize_t)HDR_LEN && packet_check(in, (size_t)got, TYPE_ACK, 7) == PKT_OK
          && read_u32(in, OFF_NUM) == 5, "it is ACK 5 and passes packet_check");

    close(snd);
    close(rcv);
}

/*
    4. UDP keeps datagram boundaries: each recvfrom() returns exactly one datagram,
    so one datagram is always exactly one packet. an empty datagram is valid UDP
    and reads as n = 0, which packet_check rejects as PKT_SHORT.
 */
static void test_boundaries(void)
{
    static uint8_t buffer[MAX_PKT];
    int a = udp_open(0), b = udp_open(0);
    check(a >= 0 && b >= 0, "two sockets open");
    if (a < 0 || b < 0)
        return;

    uint16_t port = udp_local_port(b);
    memset(buffer, 1, 100);
    send_to_port(a, buffer, 100, port);
    send_to_port(a, buffer, 1, port);
    send_to_port(a, buffer, 0, port);

    ssize_t first = recv_within(b, buffer, sizeof(buffer));
    ssize_t second = recv_within(b, buffer, sizeof(buffer));
    ssize_t third = recv_within(b, buffer, sizeof(buffer));
    check(first == 100 && second == 1, "100 + 1 bytes sent: two reads of 100 and 1, not one of 101");
    check(third == 0, "an empty datagram reads as 0 bytes");
    check(packet_check(buffer, 0, TYPE_DATA, 7) == PKT_SHORT, "and packet_check drops it as short");

    close(a);
    close(b);
}

/*
    5. MAX_PKT (65507) is the largest datagram UDP over IPv4 can carry: it arrives
    whole into a MAX_PKT buffer, and one byte more is refused by sendto().
 */
static void test_max_packet(void)
{
    static uint8_t out[MAX_PKT + 1], in[MAX_PKT];
    int a = udp_open(0), b = udp_open(0);
    check(a >= 0 && b >= 0, "two sockets open");
    if (a < 0 || b < 0)
        return;

    uint16_t port = udp_local_port(b);
    for (size_t i = 0; i < sizeof(out); i++)
        out[i] = (uint8_t)(i * 7 + 3);

    check(send_to_port(a, out, MAX_PKT, port) == MAX_PKT, "a MAX_PKT datagram is sent");
    ssize_t got = recv_within(b, in, sizeof(in));
    check(got == MAX_PKT && memcmp(in, out, MAX_PKT) == 0, "and arrives whole into a MAX_PKT buffer");

    errno = 0;
    check(send_to_port(a, out, MAX_PKT + 1, port) == -1 && errno == EMSGSIZE, "MAX_PKT + 1 bytes: sendto fails with EMSGSIZE");

    close(a);
    close(b);
}

/*
    6. udp_wait when nothing arrives: returns 0 after about timeout_ms, never earlier.
    a closed descriptor is an error, not "ready".
 */
static void test_wait_timeout(void)
{
    int fd = udp_open(0);
    check(fd >= 0, "a socket opens");
    if (fd < 0)
        return;

    double t0 = now_ms();
    check(udp_wait(fd, 0) == 0, "timeout 0 with nothing waiting returns 0");
    check(now_ms() - t0 < 10.0, "and returns at once");

    t0 = now_ms();
    int r = udp_wait(fd, 50);
    double waited = now_ms() - t0;
    check(r == 0, "timeout 50 ms with nothing waiting returns 0");
    // poll() never returns early; the upper bound is generous so a busy CI machine does not fail it
    check(waited >= 49.0 && waited < 500.0, "after about 50 ms");

    close(fd);
    errno = 0;
    check(udp_wait(fd, 0) == -1 && errno == EBADF, "a closed descriptor: -1 with errno EBADF");
}

int main(void)
{
    puts("udp:");
    test_addr();
    test_open();
    test_round_trip();
    test_boundaries();
    test_max_packet();
    test_wait_timeout();

    if (failures > 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    puts("all checks passed");
    return 0;
}
