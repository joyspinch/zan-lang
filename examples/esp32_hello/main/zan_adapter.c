#include <stdarg.h>
#include <stdio.h>

int main(int argc, char **argv);

void app_main(void) {
    main(0, (char **)0);
}

int zan_w32_snprintf(char *s, long long n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, (size_t)n, fmt, ap);
    va_end(ap);
    return r;
}
