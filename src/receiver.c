/* Receiver: validates packets, sends ACKs and rebuilds the file (#6, #11, #12). */
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cli.h"
#include "log.h"
#include "packet.h"
#include "udp.h"

/* state shared by every protocol; GBN and SR add their own fields here */
struct receiver {
    const struct config *cfg;
    int fd;
    FILE *out;         /* NULL once the file is complete */
    uint32_t modulus;  /* 2^k */
    uint32_t expected; /* SW and GBN: the next sequence number to deliver */
    uint64_t bytes;    /* delivered so far */
    int complete;      /* LAST delivered and the file closed */
    int failed;        /* the output file could not be written */
};

#define NO_ACK (-1)

/*
    one per protocol, called for every DATA packet that passed packet_check().
    returns the number to acknowledge, or NO_ACK to send nothing.
 */
typedef int (*data_handler)(struct receiver *r, uint32_t num, const uint8_t *payload,
                            uint16_t len, uint8_t flags);

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

    fprintf(stderr, "receiver: %s: %s\n", what, reason);
    log_event("error", 0, 0, 0, 0, "%s: %s", what, reason);
}

/* appends one in-order payload to the file; the LAST packet completes it (section 4.2) */
static void deliver(struct receiver *r, uint32_t num, const uint8_t *payload, uint16_t len,
                    uint8_t flags)
{
    if (len > 0 && fwrite(payload, 1, len, r->out) != len) {
        report_error("cannot write %s", r->cfg->file);
        r->failed = 1;
        return;
    }
    r->bytes += len;
    log_event("deliver", TYPE_DATA, num, len, flags, NULL);

    if (flags & FLAG_LAST) {
        // fclose also flushes, so a full disk shows up here
        int err = fclose(r->out);
        r->out = NULL;
        if (err != 0) {
            report_error("cannot write %s", r->cfg->file);
            r->failed = 1;
            return;
        }
        r->complete = 1;
        log_event("complete", 0, 0, 0, 0, "bytes=%" PRIu64, r->bytes);
    }
}

/*
    Stop-and-Wait (section 3.1): num == expected is delivered, anything else is a
    duplicate and discarded. both are acknowledged with ACK num.
 */
static int sw_on_data(struct receiver *r, uint32_t num, const uint8_t *payload, uint16_t len,
                      uint8_t flags)
{
    // after LAST the file is closed: nothing is delivered again, whatever num is
    if (num == r->expected && !r->complete) {
        deliver(r, num, payload, len, flags);
        r->expected = (r->expected + 1) % r->modulus;
    } else {
        log_event("discard", TYPE_DATA, num, len, flags, "expected=%" PRIu32, r->expected);
    }
    return (int)num;
}

/* sends ACK num back to where the DATA packet came from, i.e. the emulator (section 1) */
static void send_ack(struct receiver *r, uint32_t num, const struct sockaddr_in *to,
                     socklen_t to_len)
{
    uint8_t pkt[HDR_LEN];
    size_t n = packet_build(pkt, TYPE_ACK, 0, num, NULL, 0);

    // not fatal: to the sender a failed send looks like a lost ACK
    if (sendto(r->fd, pkt, n, 0, (const struct sockaddr *)to, to_len) < 0)
        log_event("ack_fail", TYPE_ACK, num, 0, 0, "%s", strerror(errno));
    else
        log_event("ack", TYPE_ACK, num, 0, 0, NULL);
}

/*
    the receive loop, the same for every protocol. it waits without a limit for the first
    datagram, because the sender may start later. from then on every datagram, valid or not,
    restarts the linger period, and the receiver exits once a whole period passes in silence:
    with 0 if the file is complete, with 1 if it is not, because then the sender has given
    up (section 4.2). returns the exit status.
 */
static int run(struct receiver *r, data_handler on_data)
{
    static uint8_t buf[MAX_PKT];
    int linger = (int)r->cfg->linger_ms; // the CLI keeps it <= INT_MAX, as poll() takes an int
    int started = 0;                     // a datagram has arrived

    for (;;) {
        int ready = udp_wait(r->fd, started ? linger : -1);
        if (ready < 0) {
            report_error("poll");
            return 2;
        }
        if (ready == 0) {
            if (!r->complete) {
                fprintf(stderr, "receiver: no datagram for %d ms and the file is not complete, "
                                "giving up\n", linger);
                log_event("give_up", 0, 0, 0, 0, "linger_ms=%d bytes=%" PRIu64, linger, r->bytes);
                return 1;
            }
            log_event("exit", 0, 0, 0, 0, "linger_ms=%d", linger);
            return 0;
        }

        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        ssize_t n = recvfrom(r->fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            report_error("recvfrom");
            return 2;
        }
        started = 1;

        // header fields are not trusted here, so only the size is logged; no reply (section 2.5)
        enum pkt_status s = packet_check(buf, (size_t)n, TYPE_DATA, r->cfg->k);
        if (s != PKT_OK) {
            log_event("drop", 0, 0, 0, 0, "reason=%s bytes=%zd", packet_status_str(s), n);
            continue;
        }

        uint32_t num = read_u32(buf, OFF_NUM);
        uint16_t len = read_u16(buf, OFF_LEN);
        uint8_t flags = buf[OFF_FLAGS];
        log_event("recv", TYPE_DATA, num, len, flags, NULL);

        int ack = on_data(r, num, buf + HDR_LEN, len, flags);
        if (r->failed)
            return 2;
        if (ack != NO_ACK)
            send_ack(r, (uint32_t)ack, &from, from_len);
    }
}

/* opens the output file and the socket, runs the transfer, closes both; returns the exit status */
static int receive_file(const struct config *cfg, data_handler on_data)
{
    struct receiver r = { .cfg = cfg, .fd = -1, .modulus = 1u << cfg->k };
    int status = 2;

    // both are opened before any traffic, so a bad path or a busy port fails at once
    r.out = fopen(cfg->file, "wb");
    if (r.out == NULL) {
        report_error("cannot open %s", cfg->file);
    } else if ((r.fd = udp_open(cfg->port)) < 0) {
        report_error("cannot bind 127.0.0.1:%u", (unsigned)cfg->port);
    } else {
        log_event("start", 0, 0, 0, 0, "proto=%s w=%u k=%u port=%u linger_ms=%u file=%s",
                  protocol_name(cfg->proto), cfg->w, cfg->k, (unsigned)cfg->port,
                  cfg->linger_ms, cfg->file);
        if (!window_is_safe(cfg)) {
            unsigned limit = max_safe_window(cfg->proto, cfg->k);
            fprintf(stderr, "receiver: warning: window %u is above the safe limit %u for %s\n",
                    cfg->w, limit, protocol_name(cfg->proto));
            log_event("warn", 0, 0, 0, 0, "window %u above safe limit %u", cfg->w, limit);
        }
        status = run(&r, on_data);
    }

    if (r.out != NULL) // still open if the transfer did not complete
        fclose(r.out);
    if (r.fd >= 0)
        close(r.fd);
    return status;
}

int main(int argc, char **argv)
{
    struct config cfg;

    switch (cli_parse(argc, argv, ROLE_RECEIVER, &cfg)) {
    case CLI_HELP:  return 0;
    case CLI_ERROR: return 2;
    case CLI_OK:    break;
    }

    data_handler on_data = NULL;
    switch (cfg.proto) {
    case PROTO_SW:
        on_data = sw_on_data;
        break;
    case PROTO_GBN: // #11
    case PROTO_SR:  // #12
        fprintf(stderr, "receiver: -p %s is not implemented yet\n", protocol_name(cfg.proto));
        return 2;
    }

    if (log_open(cfg.log_path, "receiver") < 0) {
        fprintf(stderr, "receiver: cannot open log %s: %s\n", cfg.log_path, strerror(errno));
        return 2;
    }

    int status = receive_file(&cfg, on_data);

    // an incomplete log makes the run useless for the analysis, so it counts as a failure
    if (log_close() < 0) {
        fprintf(stderr, "receiver: cannot write log %s\n", cfg.log_path);
        status = 2;
    }
    return status;
}
