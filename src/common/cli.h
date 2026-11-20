/* Command line of sender and receiver (docs/interfaces.md section 5, issue #6). */
#ifndef CLI_H
#define CLI_H

#include <stdint.h>

enum protocol { PROTO_SW, PROTO_GBN, PROTO_SR };
enum role     { ROLE_SENDER, ROLE_RECEIVER };

/* defaults of section 5.1. -r and -x are provisional until the RTO work in #13 */
#define DEFAULT_W           16
#define DEFAULT_K           7
#define DEFAULT_MAX_PAYLOAD 1024
#define DEFAULT_MIN_RTO_MS  200
#define DEFAULT_MAX_RETX    10
#define DEFAULT_LINGER_MS   5000

/*
    every run-time parameter of one program.
    file and log_path point into argv, which lives as long as the program.
 */
struct config {
    enum protocol proto;
    unsigned w;           /* window size; always 1 with sw */
    unsigned k;           /* sequence-number width in bits, 3..8 */
    unsigned max_payload; /* sender only */
    const char *file;     /* sender: file to send, receiver: file to write */
    uint16_t port;        /* sender: emulator port E, receiver: own port R */
    const char *log_path;
    unsigned min_rto_ms;  /* sender only; in #6 also the fixed timeout */
    unsigned max_retx;    /* sender only */
    unsigned linger_ms;   /* receiver only */
};

enum cli_result {
    CLI_OK,    /* cfg is filled in, run the program */
    CLI_HELP,  /* -h: usage printed to stdout, exit with 0 */
    CLI_ERROR, /* reason and usage printed to stderr, exit with 2 */
};

/*
    parses argv with getopt() and checks every value against section 5.2.
    role decides which options are accepted: -m -r -x for the sender, -q for the receiver.
    never exits, so the tests can call it many times.
 */
enum cli_result cli_parse(int argc, char **argv, enum role role, struct config *cfg);

/* largest window that is still safe: 1 for sw, 2^k - 1 for gbn, 2^(k-1) for sr */
unsigned max_safe_window(enum protocol proto, unsigned k);

/*
    whether cfg->w is within the safe limit. a larger window is allowed
    (Experiment 5 needs it), but the program warns and logs it.
 */
int window_is_safe(const struct config *cfg);

/* "sw", "gbn" or "sr", as written on the command line */
const char *protocol_name(enum protocol proto);

#endif
