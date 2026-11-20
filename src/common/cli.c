/* Command line of sender and receiver: getopt parsing and validation (issue #6). */
#include "cli.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "packet.h"

static const char *role_name(enum role role)
{
    return role == ROLE_SENDER ? "sender" : "receiver";
}

static void usage(FILE *out, enum role role)
{
    if (role == ROLE_SENDER) {
        fprintf(out,
                "usage: sender -p sw|gbn|sr -f FILE -P PORT -L LOG [-w W] [-k K] [-m BYTES] [-r MS] [-x N]\n"
                "  -p  protocol: sw, gbn or sr (required)\n"
                "  -f  file to send (required)\n"
                "  -P  emulator port to send to, 1..65535 (required)\n"
                "  -L  log file (required)\n"
                "  -w  window size, 1..2^k (default %d, ignored with sw)\n"
                "  -k  sequence-number width in bits, 3..8 (default %d)\n"
                "  -m  max payload in bytes, 1..%d (default %d)\n"
                "  -r  minimum RTO in ms (default %d)\n"
                "  -x  retransmissions of one packet before giving up (default %d)\n"
                "  -h  print this help\n",
                DEFAULT_W, DEFAULT_K, MAX_PAYLOAD, DEFAULT_MAX_PAYLOAD,
                DEFAULT_MIN_RTO_MS, DEFAULT_MAX_RETX);
    } else {
        fprintf(out,
                "usage: receiver -p sw|gbn|sr -f FILE -P PORT -L LOG [-w W] [-k K] [-q MS]\n"
                "  -p  protocol: sw, gbn or sr (required)\n"
                "  -f  file to write (required)\n"
                "  -P  port to listen on, 1..65535 (required)\n"
                "  -L  log file (required)\n"
                "  -w  window size, 1..2^k (default %d, ignored with sw)\n"
                "  -k  sequence-number width in bits, 3..8 (default %d)\n"
                "  -q  linger time in ms after the file is complete (default %d)\n"
                "  -h  print this help\n",
                DEFAULT_W, DEFAULT_K, DEFAULT_LINGER_MS);
    }
}

/* prints why a value was rejected, then the usage */
static enum cli_result bad_value(enum role role, int opt, const char *value, const char *allowed)
{
    fprintf(stderr, "%s: invalid value '%s' for -%c (allowed: %s)\n",
            role_name(role), value, opt, allowed);
    usage(stderr, role);
    return CLI_ERROR;
}

/*
    reads a decimal number from s and checks min <= value <= max.
    rejects what atoi() would accept silently: "", "12abc", and "-1",
    which strtoul() turns into ULONG_MAX. base 10, so "010" is ten, not octal.
 */
static int parse_uint(const char *s, unsigned long min, unsigned long max, unsigned long *out)
{
    if (*s < '0' || *s > '9') /* also rejects "", "-", "+" and leading spaces */
        return 0;

    char *end;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno == ERANGE || *end != '\0' || v < min || v > max)
        return 0;

    *out = v;
    return 1;
}

static int parse_protocol(const char *s, enum protocol *out)
{
    if (strcmp(s, "sw") == 0)
        *out = PROTO_SW;
    else if (strcmp(s, "gbn") == 0)
        *out = PROTO_GBN;
    else if (strcmp(s, "sr") == 0)
        *out = PROTO_SR;
    else
        return 0;
    return 1;
}

enum cli_result cli_parse(int argc, char **argv, enum role role, struct config *cfg)
{
    const char *optstring = role == ROLE_SENDER ? "p:w:k:m:f:P:L:r:x:h" : "p:w:k:f:P:L:q:h";

    *cfg = (struct config){
        .proto       = PROTO_SW,
        .w           = DEFAULT_W,
        .k           = DEFAULT_K,
        .max_payload = DEFAULT_MAX_PAYLOAD,
        .file        = NULL,
        .port        = 0,
        .log_path    = NULL,
        .min_rto_ms  = DEFAULT_MIN_RTO_MS,
        .max_retx    = DEFAULT_MAX_RETX,
        .linger_ms   = DEFAULT_LINGER_MS,
    };
    int have_proto = 0;
    unsigned long v;

    /*
        getopt() keeps its position in globals. 0 (not 1) makes glibc restart from argv[1]
        and forget a half-read group such as "-hk3" left by an earlier call; the tests need this.
     */
    optind = 0;

    /* the loop only collects values; checks that need several options come after it */
    int c;
    while ((c = getopt(argc, argv, optstring)) != -1) {
        switch (c) {
        case 'p':
            if (!parse_protocol(optarg, &cfg->proto))
                return bad_value(role, c, optarg, "sw, gbn or sr");
            have_proto = 1;
            break;
        case 'w': /* the 2^k limit needs -k, which may come later */
            if (!parse_uint(optarg, 1, UINT_MAX, &v))
                return bad_value(role, c, optarg, "1..2^k");
            cfg->w = (unsigned)v;
            break;
        case 'k':
            if (!parse_uint(optarg, 3, 8, &v))
                return bad_value(role, c, optarg, "3..8");
            cfg->k = (unsigned)v;
            break;
        case 'm':
            if (!parse_uint(optarg, 1, MAX_PAYLOAD, &v))
                return bad_value(role, c, optarg, "1..65495");
            cfg->max_payload = (unsigned)v;
            break;
        case 'f':
            if (*optarg == '\0')
                return bad_value(role, c, optarg, "a file name");
            cfg->file = optarg;
            break;
        case 'P':
            if (!parse_uint(optarg, 1, 65535, &v))
                return bad_value(role, c, optarg, "1..65535");
            cfg->port = (uint16_t)v;
            break;
        case 'L':
            if (*optarg == '\0')
                return bad_value(role, c, optarg, "a file name");
            cfg->log_path = optarg;
            break;
        case 'r':
            if (!parse_uint(optarg, 1, UINT_MAX, &v))
                return bad_value(role, c, optarg, "a positive integer");
            cfg->min_rto_ms = (unsigned)v;
            break;
        case 'x':
            if (!parse_uint(optarg, 1, UINT_MAX, &v))
                return bad_value(role, c, optarg, "a positive integer");
            cfg->max_retx = (unsigned)v;
            break;
        case 'q':
            if (!parse_uint(optarg, 1, UINT_MAX, &v))
                return bad_value(role, c, optarg, "a positive integer");
            cfg->linger_ms = (unsigned)v;
            break;
        case 'h':
            usage(stdout, role);
            return CLI_HELP;
        default: /* '?': unknown option or missing argument; getopt has printed which */
            usage(stderr, role);
            return CLI_ERROR;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "%s: unexpected argument '%s'\n", role_name(role), argv[optind]);
        usage(stderr, role);
        return CLI_ERROR;
    }

    int missing = 0;
    if (!have_proto)
        missing = 'p';
    else if (cfg->file == NULL)
        missing = 'f';
    else if (cfg->port == 0)
        missing = 'P';
    else if (cfg->log_path == NULL)
        missing = 'L';
    if (missing) {
        fprintf(stderr, "%s: missing required option -%c\n", role_name(role), missing);
        usage(stderr, role);
        return CLI_ERROR;
    }

    if (cfg->proto == PROTO_SW) {
        cfg->w = 1; /* -w is ignored with sw */
    } else if (cfg->w > (1u << cfg->k)) {
        fprintf(stderr, "%s: window %u is larger than 2^k = %u\n",
                role_name(role), cfg->w, 1u << cfg->k);
        usage(stderr, role);
        return CLI_ERROR;
    }

    return CLI_OK;
}

unsigned max_safe_window(enum protocol proto, unsigned k)
{
    switch (proto) {
    case PROTO_SW:  return 1;
    case PROTO_GBN: return (1u << k) - 1;
    case PROTO_SR:  return 1u << (k - 1);
    }
    return 1;
}

int window_is_safe(const struct config *cfg)
{
    return cfg->w <= max_safe_window(cfg->proto, cfg->k);
}

const char *protocol_name(enum protocol proto)
{
    switch (proto) {
    case PROTO_SW:  return "sw";
    case PROTO_GBN: return "gbn";
    case PROTO_SR:  return "sr";
    }
    return "unknown";
}
