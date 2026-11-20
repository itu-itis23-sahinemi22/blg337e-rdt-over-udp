/*
    unit tests for the provisional event log (issue #6).
    the log is written to a temporary file in /tmp, read back and removed.
    exit status 0 when every check passes, 1 otherwise.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "packet.h"

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

#define MAX_LINES 16
#define LINE_LEN  512

/* reads the file at path into lines (without the '\n'); returns the number of lines, -1 on error */
static int read_lines(const char *path, char lines[][LINE_LEN])
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return -1;

    int n = 0;
    while (n < MAX_LINES && fgets(lines[n], LINE_LEN, f) != NULL) {
        lines[n][strcspn(lines[n], "\n")] = '\0';
        n++;
    }
    fclose(f);
    return n;
}

/* the line without its timestamp: everything after the first comma ("" if there is none) */
static const char *after_ts(const char *line)
{
    const char *comma = strchr(line, ',');
    return comma != NULL ? comma + 1 : "";
}

static int count_commas(const char *s)
{
    int n = 0;
    for (; *s != '\0'; s++)
        if (*s == ',')
            n++;
    return n;
}

/*
    1. now_us: never goes backwards, and advances by the time that passed.
 */
static void test_now_us(void)
{
    uint64_t a = now_us();
    uint64_t b = now_us();
    check(b >= a, "two reads in a row: the second is not smaller");

    struct timespec five_ms = { 0, 5 * 1000 * 1000 };
    a = now_us();
    nanosleep(&five_ms, NULL);
    b = now_us();
    // the upper bound is generous so a busy CI machine does not fail it
    check(b - a >= 5000 && b - a < 500000, "after sleeping 5 ms it advanced by about 5000 us");
}

/*
    2. log_open on a path that cannot be created fails with errno set.
 */
static void test_open_error(void)
{
    errno = 0;
    check(log_open("/nonexistent-dir/x.log", "sender") == -1 && errno == ENOENT,
          "a missing directory: -1 with errno ENOENT");
}

/*
    3. the lines themselves: header row, packet fields, empty fields for "no packet",
    info sanitized and cut off, monotonic timestamps, and nothing written while closed.
 */
static void test_lines(void)
{
    char path[] = "/tmp/test_log_XXXXXX";
    int fd = mkstemp(path);
    check(fd >= 0, "a temporary file is created");
    if (fd < 0)
        return;
    close(fd);

    char long_info[1000];
    memset(long_info, 'x', sizeof(long_info) - 1);
    long_info[sizeof(long_info) - 1] = '\0';

    check(log_open(path, "sender") == 0, "log_open succeeds");
    log_event("start", 0, 0, 0, 0, "proto=%s w=%u", "sw", 1u);
    log_event("send", TYPE_DATA, 5, 1024, FLAG_LAST, NULL);
    log_event("ack", TYPE_ACK, 5, 0, 0, NULL);
    log_event("drop", 9, 3, 0, 0, "reason=%s", "bad_type");
    log_event("note", 0, 0, 0, 0, "a,b\nc");
    log_event("note", 0, 0, 0, 0, "%s", long_info);
    check(log_close() == 0, "log_close returns 0");

    log_event("late", 0, 0, 0, 0, NULL); // no log open: must be ignored, not crash

    static char lines[MAX_LINES][LINE_LEN];
    int n = read_lines(path, lines);
    check(n == 7, "header + 6 events = 7 lines; the event after log_close is not written");
    if (n != 7) {
        unlink(path);
        return;
    }

    check(strcmp(lines[0], "ts_us,component,event,type,num,len,flags,info") == 0, "the first line is the header row");
    check(strcmp(after_ts(lines[1]), "sender,start,,,,,proto=sw w=1") == 0, "no packet: type, num, len and flags are empty; info is formatted");
    check(strcmp(after_ts(lines[2]), "sender,send,data,5,1024,2,") == 0, "DATA packet: data,5,1024,2 and an empty info");
    check(strcmp(after_ts(lines[3]), "sender,ack,ack,5,0,0,") == 0, "ACK packet: ack,5,0,0");
    check(strcmp(after_ts(lines[4]), "sender,drop,9,3,0,0,reason=bad_type") == 0, "an unknown type is written as its number");
    
    check(strcmp(after_ts(lines[5]), "sender,note,,,,,a;b c") == 0, "a comma in info becomes ';' and a line break a space");
    check(strlen(after_ts(lines[6])) == strlen("sender,note,,,,,") + 255, "a long info is cut off at 255 characters");

    int fields_ok = 1, ts_ok = 1;
    unsigned long long prev = 0;
    for (int i = 1; i < n; i++) {
        char *end;
        unsigned long long ts = strtoull(lines[i], &end, 10);
        if (count_commas(lines[i]) != 7)
            fields_ok = 0;
        if (end == lines[i] || *end != ',' || ts < prev)
            ts_ok = 0;
        prev = ts;
    }
    check(fields_ok, "every line has exactly 8 fields (7 commas)");
    check(ts_ok, "every line starts with a timestamp, and they never go backwards");

    unlink(path);
}

/*
    4. a write that fails is reported by log_close. /dev/full accepts open() but every
    write fails with ENOSPC, like a full disk.
 */
static void test_write_error(void)
{
    check(log_open("/dev/full", "receiver") == 0, "log_open(/dev/full) succeeds");
    log_event("start", 0, 0, 0, 0, NULL);
    check(log_close() == -1, "log_close reports the failed write with -1");
}

int main(void)
{
    puts("log:");
    test_now_us();
    test_open_error();
    test_lines();
    test_write_error();

    if (failures > 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    puts("all checks passed");
    return 0;
}
