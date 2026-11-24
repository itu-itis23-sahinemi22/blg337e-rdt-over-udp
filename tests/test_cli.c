/*
    unit tests for the sender/receiver command line (issue #6).
    exit status 0 when every check passes, 1 otherwise.
 */
#include <stdio.h>
#include <string.h>

#include "cli.h"

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

/*
    runs cli_parse on a NULL-terminated argument list. argv[0] is "test".
    PARSE(role, &cfg, "-p", "sw", ...) builds the list in place.
 */
static enum cli_result parse_list(enum role role, struct config *cfg, char **argv)
{
    int argc = 0;
    while (argv[argc] != NULL)
        argc++;
    return cli_parse(argc, argv, role, cfg);
}

#define PARSE(role, cfg, ...) parse_list((role), (cfg), (char *[]){"test", __VA_ARGS__, NULL})

/* the required options, so each test only adds what it is about */
#define SENDER_REQUIRED   "-p", "gbn", "-f", "in.bin", "-P", "9001", "-L", "sender.log"
#define RECEIVER_REQUIRED "-p", "gbn", "-f", "out.bin", "-P", "9002", "-L", "receiver.log"

/*
    1. defaults: only the required options give the baseline of section 5.1.
 */
static void test_defaults(void)
{
    struct config cfg;

    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED) == CLI_OK, "sender with only the required options is ok");
    check(cfg.proto == PROTO_GBN && strcmp(cfg.file, "in.bin") == 0 && cfg.port == 9001
          && strcmp(cfg.log_path, "sender.log") == 0, "sender: -p, -f, -P and -L are stored");
    check(cfg.w == 16 && cfg.k == 7 && cfg.max_payload == 1024, "sender defaults: w 16, k 7, m 1024");
    check(cfg.min_rto_ms == 200 && cfg.max_retx == 10, "sender defaults: r 200, x 10");

    check(PARSE(ROLE_RECEIVER, &cfg, RECEIVER_REQUIRED) == CLI_OK, "receiver with only the required options is ok");
    check(cfg.linger_ms == 5000 && cfg.port == 9002, "receiver defaults: q 5000");
}

/*
    2. every option is stored where it belongs.
 */
static void test_all_options(void)
{
    struct config cfg;

    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-p", "sr", "-w", "8", "-k", "4",
                "-m", "512", "-r", "50", "-x", "3") == CLI_OK, "sender with every option is ok");
    check(cfg.proto == PROTO_SR && cfg.w == 8 && cfg.k == 4 && cfg.max_payload == 512
          && cfg.min_rto_ms == 50 && cfg.max_retx == 3, "sender: every value is stored (a later -p wins)");

    check(PARSE(ROLE_RECEIVER, &cfg, RECEIVER_REQUIRED, "-q", "1500") == CLI_OK && cfg.linger_ms == 1500,
          "receiver: -q is stored");
}

/*
    3. the window: checked against 2^k after all options are read, so the order of -w and -k
    does not matter. ignored with sw. above the safe limit it is still accepted.
 */
static void test_window(void)
{
    struct config cfg;

    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-w", "200", "-k", "8") == CLI_OK && cfg.w == 200,
          "-w 200 -k 8 is ok");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-k", "8", "-w", "200") == CLI_OK && cfg.w == 200,
          "-k 8 -w 200 is ok (order does not matter)");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-w", "200") == CLI_ERROR,
          "-w 200 with the default k = 7 is an error (2^7 = 128)");

    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-k", "3", "-w", "8") == CLI_OK, "-k 3 -w 8 is ok (w = 2^k)");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-k", "3", "-w", "9") == CLI_ERROR, "-k 3 -w 9 is an error");

    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-p", "sw", "-w", "50") == CLI_OK && cfg.w == 1,
          "-p sw -w 50 is ok and the window is 1");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-p", "sw", "-w", "abc") == CLI_ERROR,
          "-p sw -w abc is still an error (not a number)");
}

/*
    4. safe window limits: 1 for sw, 2^k - 1 for gbn, 2^(k-1) for sr.
 */
static void test_safe_window(void)
{
    struct config cfg;

    check(max_safe_window(PROTO_SW, 3) == 1 && max_safe_window(PROTO_GBN, 3) == 7
          && max_safe_window(PROTO_SR, 3) == 4, "k = 3: sw 1, gbn 7, sr 4");
    check(max_safe_window(PROTO_GBN, 8) == 255 && max_safe_window(PROTO_SR, 8) == 128, "k = 8: gbn 255, sr 128");

    PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-k", "3", "-w", "7");
    check(window_is_safe(&cfg), "gbn k = 3 w = 7 is safe");
    PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-k", "3", "-w", "8");
    check(!window_is_safe(&cfg), "gbn k = 3 w = 8 is accepted but not safe");
    PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-p", "sr", "-k", "3", "-w", "5");
    check(!window_is_safe(&cfg), "sr k = 3 w = 5 is accepted but not safe");
}

/*
    5. values outside section 5.2, and strings that atoi() would accept silently.
 */
static void test_bad_values(void)
{
    struct config cfg;

    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-p", "tcp") == CLI_ERROR, "-p tcp");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-k", "2") == CLI_ERROR, "-k 2");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-k", "9") == CLI_ERROR, "-k 9");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-w", "0") == CLI_ERROR, "-w 0");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-m", "0") == CLI_ERROR, "-m 0");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-m", "65496") == CLI_ERROR, "-m 65496 (MAX_PAYLOAD + 1)");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-m", "65495") == CLI_OK, "-m 65495 (MAX_PAYLOAD) is ok");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-P", "0") == CLI_ERROR, "-P 0");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-P", "65536") == CLI_ERROR, "-P 65536");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-x", "0") == CLI_ERROR, "-x 0");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-m", "12abc") == CLI_ERROR, "-m 12abc (trailing characters)");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-r", "-1") == CLI_ERROR, "-r -1 (strtoul would give ULONG_MAX)");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-P", "+9001") == CLI_ERROR, "-P +9001 (sign)");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-k", "") == CLI_ERROR, "-k \"\" (empty)");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-r", "99999999999999999999999") == CLI_ERROR,
          "-r 99999999999999999999999 (overflow)");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-r", "2147483647") == CLI_OK,
          "-r 2147483647 (INT_MAX) is ok");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-r", "2147483648") == CLI_ERROR,
          "-r 2147483648 (INT_MAX + 1, too long for poll())");
    check(PARSE(ROLE_RECEIVER, &cfg, RECEIVER_REQUIRED, "-q", "2147483647") == CLI_OK,
          "-q 2147483647 (INT_MAX) is ok");
    check(PARSE(ROLE_RECEIVER, &cfg, RECEIVER_REQUIRED, "-q", "2147483648") == CLI_ERROR,
          "-q 2147483648 (INT_MAX + 1, too long for poll())");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-x", "4294967295") == CLI_OK,
          "-x 4294967295 (UINT_MAX) is ok: it never reaches poll()");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-f", "") == CLI_ERROR, "-f \"\" (empty file name)");
}

/*
    6. missing required options, unknown options, a missing argument and an extra argument.
 */
static void test_usage_errors(void)
{
    struct config cfg;

    check(PARSE(ROLE_SENDER, &cfg, "-f", "in.bin", "-P", "9001", "-L", "s.log") == CLI_ERROR, "missing -p");
    check(PARSE(ROLE_SENDER, &cfg, "-p", "sw", "-P", "9001", "-L", "s.log") == CLI_ERROR, "missing -f");
    check(PARSE(ROLE_SENDER, &cfg, "-p", "sw", "-f", "in.bin", "-L", "s.log") == CLI_ERROR, "missing -P");
    check(PARSE(ROLE_SENDER, &cfg, "-p", "sw", "-f", "in.bin", "-P", "9001") == CLI_ERROR, "missing -L");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-z") == CLI_ERROR, "unknown option -z");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-k") == CLI_ERROR, "-k without a value");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "extra") == CLI_ERROR, "extra argument");
}

/*
    7. each program accepts only its own options: -m -r -x are sender-only, -q is receiver-only.
 */
static void test_role_options(void)
{
    struct config cfg;

    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED, "-q", "100") == CLI_ERROR, "sender rejects -q");
    check(PARSE(ROLE_RECEIVER, &cfg, RECEIVER_REQUIRED, "-m", "512") == CLI_ERROR, "receiver rejects -m");
    check(PARSE(ROLE_RECEIVER, &cfg, RECEIVER_REQUIRED, "-r", "50") == CLI_ERROR, "receiver rejects -r");
    check(PARSE(ROLE_RECEIVER, &cfg, RECEIVER_REQUIRED, "-x", "3") == CLI_ERROR, "receiver rejects -x");
}

/*
    8. -h asks for the usage, even when the rest of the line is incomplete.
    the next parse must start clean although -h stopped getopt in the middle of "-hk3".
 */
static void test_help(void)
{
    struct config cfg;

    check(PARSE(ROLE_SENDER, &cfg, "-hk3") == CLI_HELP, "-h gives CLI_HELP");
    check(PARSE(ROLE_SENDER, &cfg, SENDER_REQUIRED) == CLI_OK && cfg.k == 7,
          "the next parse starts clean (k is not 3)");
}

/*
    9. protocol names, as they will appear in the logs.
 */
static void test_protocol_name(void)
{
    check(strcmp(protocol_name(PROTO_SW), "sw") == 0 && strcmp(protocol_name(PROTO_GBN), "gbn") == 0
          && strcmp(protocol_name(PROTO_SR), "sr") == 0, "protocol names are sw, gbn and sr");
}

int main(void)
{
    /* the error cases print a reason and the usage to stderr; silence it so the results stay readable */
    if (freopen("/dev/null", "w", stderr) == NULL)
        return 1;

    puts("cli:");
    test_defaults();
    test_all_options();
    test_window();
    test_safe_window();
    test_bad_values();
    test_usage_errors();
    test_role_options();
    test_help();
    test_protocol_name();

    if (failures > 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    puts("all checks passed");
    return 0;
}
