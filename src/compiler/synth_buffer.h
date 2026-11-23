#ifndef ZAN_SYNTH_BUFFER_H
#define ZAN_SYNTH_BUFFER_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} zan_synth_buf_t;

static inline void zan_synth_putf(zan_synth_buf_t *b, const char *fmt, ...) {
  if (!b || !fmt)
    return;
  if (!b->buf) {
    b->cap = 16384;
    b->buf = (char *)malloc(b->cap);
    if (!b->buf) {
      b->cap = 0;
      return;
    }
    b->buf[0] = 0;
  }
  for (;;) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n < 0)
      return;
    if ((size_t)n < b->cap - b->len) {
      b->len += (size_t)n;
      return;
    }
    size_t ncap = b->cap * 2;
    while (ncap < b->len + (size_t)n + 1)
      ncap *= 2;
    char *grown = (char *)realloc(b->buf, ncap);
    if (!grown)
      return;
    b->buf = grown;
    b->cap = ncap;
  }
}

#endif
