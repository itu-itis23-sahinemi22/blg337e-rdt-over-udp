
#ifndef LOG_H
#define LOG_H

#include <stdint.h>

/* microseconds on CLOCK_MONOTONIC: never jumps, and the same clock in all three programs */
uint64_t now_us(void);

/*
    opens (and truncates) path and writes the header row. component must outlive the log.
    returns 0, or -1 with errno set.
 */
int log_open(const char *path, const char *component);

/*
    writes one line: ts_us,component,event,type,num,len,flags,info
    type 0 means no packet: type, num, len and flags stay empty.
    info is printf-style and may be NULL; commas become ';' and line breaks spaces,
    so every line has 8 fields. does nothing while no log is open.
 */
void log_event(const char *event, uint8_t type, uint32_t num, unsigned len, uint8_t flags,
               const char *fmt, ...) __attribute__((format(printf, 6, 7)));

/* flushes and closes the log. returns 0, or -1 if any line could not be written */
int log_close(void);

#endif
