#pragma once

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

/*
 * Checked fixed-buffer formatting for packaging paths/commands.
 *
 * Formats `fmt`/... into `buf` and fails instead of silently truncating when
 * the result does not fit. On failure a diagnostic names the buffer (`what`),
 * the required length, the available capacity and the full formatted value
 * (rendered into a temporary heap buffer, error path only).
 *
 * Returns 0 when the value was written completely, -1 on truncation or
 * formatting failure. Callers must take their failure path on -1.
 */
static inline int zan_fmt_checked(char *buf, size_t cap, const char *what,
                                  const char *fmt, ...) {
    const char *label = what ? what : "buffer";
    if (!buf || cap == 0) {
        fprintf(stderr, "error: %s: empty formatting target\n", label);
        return -1;
    }

    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);

    if (need < 0) {
        fprintf(stderr, "error: %s: formatting failed\n", label);
        return -1;
    }
    if ((size_t)need < cap) return 0;

    /* Truncated: re-render into a sized temporary so the diagnostic can show
     * the whole offending value, not just the prefix that fit. */
    char *full = (char *)malloc((size_t)need + 1);
    if (full) {
        va_start(ap, fmt);
        vsnprintf(full, (size_t)need + 1, fmt, ap);
        va_end(ap);
        fprintf(stderr,
                "error: %s: value needs %d bytes but the buffer holds %zu; "
                "value: \"%s\"\n",
                label, need, cap, full);
        free(full);
    } else {
        fprintf(stderr,
                "error: %s: value needs %d bytes but the buffer holds %zu "
                "(value prefix: \"%.*s\")\n",
                label, need, cap, (int)(cap > 1 ? cap - 1 : 0), buf);
    }
    return -1;
}
