/* UDP sockets on 127.0.0.1, shared by sender, receiver and emulator (docs/interfaces.md section 1, issue #6). */
#ifndef UDP_H
#define UDP_H

#include <netinet/in.h>
#include <stdint.h>

/*
    usage:
    receiver: fd = udp_open(R); then recvfrom() into a MAX_PKT buffer and
      sendto() each ACK to the address recvfrom() returned.
    sender:   fd = udp_open(0); dst = udp_addr(E); then sendto(fd, ..., &dst).

    sendto() and recvfrom() are called directly; only the parts that are easy
    to get wrong are wrapped here.
    the sender socket is never connect()ed: on Linux a connected UDP socket
    reports an ICMP "port unreachable" (emulator not running yet) as
    ECONNREFUSED on a later recv(). an unconnected one never sees it.
 */

/*
    opens a UDP socket bound to 127.0.0.1:port. port 0 lets the OS choose a free port (the sender).
    returns the descriptor, or -1 with errno set (e.g. EADDRINUSE); printing the error is
    left to the caller, which exits with status 2 (section 5.3).
 */
int udp_open(uint16_t port);

/* the address 127.0.0.1:port, ready for sendto() and bind() */
struct sockaddr_in udp_addr(uint16_t port);

/* the port fd is bound to, in host byte order, or 0 on error */
uint16_t udp_local_port(int fd);

/*
    waits until a datagram can be read from fd, for at most timeout_ms (-1: no limit).
    returns 1 when one is waiting, 0 when the time is up, -1 on error (errno set).
    after an interrupting signal it waits again for the full timeout_ms. so a caller
    that needs a fixed deadline (the sender's retransmission timer) passes the time
    left until the deadline, never a fixed period.
 */
int udp_wait(int fd, int timeout_ms);

#endif
