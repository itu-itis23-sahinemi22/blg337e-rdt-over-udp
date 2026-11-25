/* Shared event logging implementation. */
#include "log.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "packet.h"

static FILE *log_file = NULL;
static const char *log_component = NULL;

uint64_t now_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

int log_open(const char *path, const char *component)
{
    // fully buffered: a write() per line would change the timings we measure
    log_file = fopen(path, "w");
    if (log_file == NULL)
        return -1;

    log_component = component;
    fputs("ts_us,component,event,type,num,len,flags,info\n", log_file);
    return 0;
}

void log_event(const char *event, uint8_t type, uint32_t num, unsigned len, uint8_t flags,
               const char *fmt, ...)
{
    if (log_file == NULL)
        return;

    fprintf(log_file, "%" PRIu64 ",%s,%s,", now_us(), log_component, event);

    // four empty fields, each followed by a comma
    if (type == 0)
        fputs(",,,,", log_file);
    else if (type == TYPE_DATA)
        fprintf(log_file, "data,%" PRIu32 ",%u,%u,", num, len, (unsigned)flags);
    else if (type == TYPE_ACK)
        fprintf(log_file, "ack,%" PRIu32 ",%u,%u,", num, len, (unsigned)flags);
    else
        fprintf(log_file, "%u,%" PRIu32 ",%u,%u,", (unsigned)type, num, len, (unsigned)flags);

    if (fmt != NULL) {
        char info[256]; // longer info is cut off
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(info, sizeof(info), fmt, ap);
        va_end(ap);

        for (char *c = info; *c != '\0'; c++) {
            if (*c == ',')
                *c = ';';
            else if (*c == '\n' || *c == '\r')
                *c = ' ';
        }
        fputs(info, log_file);
    }
    fputc('\n', log_file);
}

int log_close(void)
{
    if (log_file == NULL)
        return 0;

    // ferror: an earlier write failed; fclose: the last flush failed
    int failed = ferror(log_file);
    if (fclose(log_file) != 0)
        failed = 1;

    log_file = NULL;
    log_component = NULL;
    return failed ? -1 : 0;
}
