/* Sender: splits the file into packets and runs SW / GBN / SR (#6, #11, #12, #13). */
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cli.h"
#include "log.h"
#include "packet.h"
#include "udp.h"

/* state shared by every protocol */
struct sender {
    const struct config *cfg;
    int fd;
    struct sockaddr_in dst; /* the emulator, 127.0.0.1:E */
    uint8_t *data;          /* the whole file */
    size_t size;
    size_t count;           /* number of DATA packets, at least 1 */
    uint32_t modulus;       /* 2^k */
};

/* prints and logs a run-time error, followed by errno's text */
static void report_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void report_error(const char *fmt, ...)
{
    const char *reason = strerror(errno); // before anything else can change errno
    char what[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(what, sizeof(what), fmt, ap);
    va_end(ap);

    fprintf(stderr, "sender: %s: %s\n", what, reason);
    log_event("error", 0, 0, 0, 0, "%s: %s", what, reason);
}

/*
    reads the whole file into memory. GBN and SR retransmit old packets, so packet i
    must stay available; in memory it is simply bytes [i*m, i*m + len).
    returns 0, or -1 with errno set.
 */
static int load_file(struct sender *s)
{
    FILE *f = fopen(s->cfg->file, "rb");
    if (f == NULL)
        return -1;

    struct stat st;
    int ok = fstat(fileno(f), &st) == 0;
    if (ok && !S_ISREG(st.st_mode)) { // a directory or a pipe has no size to read
        errno = EINVAL;
        ok = 0;
    }
    if (ok) {
        s->size = (size_t)st.st_size;
        s->data = malloc(s->size > 0 ? s->size : 1);
        ok = s->data != NULL;
    }
    if (ok) {
        errno = 0;
        if (fread(s->data, 1, s->size, f) != s->size) {
            if (errno == 0) // the file got shorter since fstat()
                errno = EIO;
            ok = 0;
        }
    }

    int saved = errno; // fclose() may change errno
    fclose(f);
    errno = saved;
    if (!ok)
        return -1;

    // an empty file is still one packet: len 0 with LAST (section 4.3)
    s->count = s->size == 0 ? 1 : (s->size + s->cfg->max_payload - 1) / s->cfg->max_payload;
    return 0;
}

/* payload length of packet i: max_payload, except for the last packet */
static uint16_t payload_len(const struct sender *s, size_t i)
{
    size_t off = i * s->cfg->max_payload;
    size_t left = s->size - off;
    return (uint16_t)(left < s->cfg->max_payload ? left : s->cfg->max_payload);
}

/* LAST is set on the last packet, on every transmission of it (section 4.1) */
static uint8_t last_flag(const struct sender *s, size_t i)
{
    return i == s->count - 1 ? FLAG_LAST : 0;
}

/*
    sends packet i (0-based) with sequence number i mod 2^k; retx sets the RETX flag.
    a failed sendto() is not fatal: the timer runs as if the packet was lost.
 */
static void send_data(struct sender *s, size_t i, int retx)
{
    static uint8_t pkt[MAX_PKT];
    uint32_t seq = (uint32_t)(i % s->modulus);
    uint16_t len = payload_len(s, i);
    uint8_t flags = (uint8_t)(last_flag(s, i) | (retx ? FLAG_RETX : 0));
    size_t n = packet_build(pkt, TYPE_DATA, flags, seq, s->data + i * s->cfg->max_payload, len);

    if (sendto(s->fd, pkt, n, 0, (const struct sockaddr *)&s->dst, sizeof(s->dst)) < 0)
        log_event("send_fail", TYPE_DATA, seq, len, flags, "%s", strerror(errno));
    else
        log_event(retx ? "retx" : "send", TYPE_DATA, seq, len, flags, NULL);
}

/*
    reads one datagram. returns 1 with *num set if it is a valid ACK,
    0 if it was dropped (logged with the reason), -1 on a socket error.
 */
static int recv_ack(struct sender *s, uint32_t *num)
{
    static uint8_t buf[MAX_PKT];
    ssize_t n = recv(s->fd, buf, sizeof(buf), 0);
    if (n < 0) {
        if (errno == EINTR)
            return 0;
        report_error("recv");
        return -1;
    }

    enum pkt_status st = packet_check(buf, (size_t)n, TYPE_ACK, s->cfg->k);
    if (st != PKT_OK) {
        log_event("drop", 0, 0, 0, 0, "reason=%s bytes=%zd", packet_status_str(st), n);
        return 0;
    }
    *num = read_u32(buf, OFF_NUM);
    return 1;
}

/* ms left until deadline (now_us() time), rounded up so poll() does not wake just before it */
static int ms_until(uint64_t deadline)
{
    uint64_t now = now_us();
    if (now >= deadline)
        return 0;
    uint64_t left = (deadline - now + 999) / 1000;
    return left > INT_MAX ? INT_MAX : (int)left;
}

/*
    Stop-and-Wait: send packet i, wait for ACK i mod 2^k, then the next one.
    the timer is a deadline, not a period: an ACK that is ignored does not restart it,
    otherwise a stream of wrong ACKs could postpone the retransmission forever.
    the timeout is fixed (-r) until the RTO estimator of #13.
    returns the exit status: 0 done, 1 gave up, 2 error.
 */
static int sw_run(struct sender *s)
{
    uint64_t timeout_us = (uint64_t)s->cfg->min_rto_ms * 1000;

    for (size_t i = 0; i < s->count; i++) {
        uint32_t seq = (uint32_t)(i % s->modulus);
        unsigned retx = 0;

        send_data(s, i, 0);
        uint64_t deadline = now_us() + timeout_us;

        for (;;) {
            int left = ms_until(deadline);
            if (left == 0) {
                log_event("timeout", TYPE_DATA, seq, payload_len(s, i), last_flag(s, i),
                          "retx=%u", retx);
                if (retx == s->cfg->max_retx) {
                    fprintf(stderr, "sender: packet %u not acknowledged after %u retransmissions, giving up\n",
                            (unsigned)seq, retx);
                    log_event("give_up", TYPE_DATA, seq, payload_len(s, i), last_flag(s, i),
                              "retx=%u", retx);
                    return 1;
                }
                retx++;
                send_data(s, i, 1);
                deadline = now_us() + timeout_us;
                continue;
            }

            int ready = udp_wait(s->fd, left);
            if (ready < 0) {
                report_error("poll");
                return 2;
            }
            if (ready == 0)
                continue; // the deadline has passed: the next turn handles the timeout

            uint32_t ack;
            int got = recv_ack(s, &ack);
            if (got < 0)
                return 2;
            if (got == 0)
                continue;
            if (ack == seq) {
                log_event("ack", TYPE_ACK, ack, 0, 0, NULL);
                break;
            }
            log_event("ack_ignored", TYPE_ACK, ack, 0, 0, "expected=%u", (unsigned)seq);
        }
    }

    // the completion-time metric ends here (section 4.1)
    log_event("done", 0, 0, 0, 0, "bytes=%zu packets=%zu", s->size, s->count);
    return 0;
}

/* loads the file, opens the socket, runs the protocol, frees both; returns the exit status */
static int send_file(const struct config *cfg, int (*run)(struct sender *))
{
    struct sender s = { .cfg = cfg, .fd = -1, .dst = udp_addr(cfg->port), .modulus = 1u << cfg->k };
    int status = 2;

    if (load_file(&s) < 0) {
        report_error("cannot read %s", cfg->file);
    } else if ((s.fd = udp_open(0)) < 0) {
        report_error("cannot open a UDP socket");
    } else {
        log_event("start", 0, 0, 0, 0,
                  "proto=%s w=%u k=%u m=%u port=%u local_port=%u timeout_ms=%u max_retx=%u "
                  "bytes=%zu packets=%zu file=%s",
                  protocol_name(cfg->proto), cfg->w, cfg->k, cfg->max_payload, (unsigned)cfg->port,
                  (unsigned)udp_local_port(s.fd), cfg->min_rto_ms, cfg->max_retx, s.size, s.count,
                  cfg->file);
        if (!window_is_safe(cfg)) {
            unsigned limit = max_safe_window(cfg->proto, cfg->k);
            fprintf(stderr, "sender: warning: window %u is above the safe limit %u for %s\n",
                    cfg->w, limit, protocol_name(cfg->proto));
            log_event("warn", 0, 0, 0, 0, "window %u above safe limit %u", cfg->w, limit);
        }
        status = run(&s);
    }

    free(s.data);
    if (s.fd >= 0)
        close(s.fd);
    return status;
}

int main(int argc, char **argv)
{
    struct config cfg;

    switch (cli_parse(argc, argv, ROLE_SENDER, &cfg)) {
    case CLI_HELP:  return 0;
    case CLI_ERROR: return 2;
    case CLI_OK:    break;
    }

    int (*run)(struct sender *) = NULL;
    switch (cfg.proto) {
    case PROTO_SW:
        run = sw_run;
        break;
    case PROTO_GBN: // #11
    case PROTO_SR:  // #12
        fprintf(stderr, "sender: -p %s is not implemented yet\n", protocol_name(cfg.proto));
        return 2;
    }

    if (log_open(cfg.log_path, "sender") < 0) {
        fprintf(stderr, "sender: cannot open log %s: %s\n", cfg.log_path, strerror(errno));
        return 2;
    }

    int status = send_file(&cfg, run);

    // an incomplete log makes the run useless for the analysis, so it counts as a failure
    if (log_close() < 0) {
        fprintf(stderr, "sender: cannot write log %s\n", cfg.log_path);
        status = 2;
    }
    return status;
}
