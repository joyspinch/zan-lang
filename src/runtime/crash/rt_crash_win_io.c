/* rt_crash_win_io.c -- Windows 崩溃路径专用无锁 IO 与栈格式化器（B-ID88）
 * fopen/fprintf 持 stdio 流锁、FILE 缓冲区走 CRT 堆锁；崩在持锁状态会把
 * 报告器自己锁死。这里只走 Win32 HANDLE + 栈缓冲 + 手写格式化器：
 * 零 CRT 锁、零堆分配（与 POSIX 侧 write(fd) 同一纪律）。 */

typedef struct zan__cf { HANDLE h; } zan__cf_t;
static zan__cf_t zan__cf_open_append(const char *path);
static void zan__cf_close(zan__cf_t f);

static void zan__cf_write(zan__cf_t f, const char *s, size_t n) {
    while (n) {
        DWORD w = 0;
        if (!WriteFile(f.h, s, (DWORD)(n > 0x7ffffff0 ? 0x7ffffff0 : n),
                       &w, NULL) || w == 0)
            return; /* 磁盘满等：安全丢弃剩余日志，不重试不崩 */
        s += w;
        n -= w;
    }
}
static void zan__cf_puts(zan__cf_t f, const char *s) {
    zan__cf_write(f, s, strlen(s));
}

/* 无符号整数按进制从 end 往左写，返回有效起始位置 */
static const char *zan__cf_dec_r(char *end, unsigned long long v) {
    do { *--end = (char)('0' + (v % 10)); v /= 10; } while (v);
    return end;
}
static const char *zan__cf_hex_r(char *end, unsigned long long v, int upper) {
    const char *d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { *--end = d[v & 15]; v >>= 4; } while (v);
    return end;
}

/* 极简 printf 子集：flags -/0、宽度、l/ll、s c d i u x X p %%。
 * 语义对齐调用点现有用法；%p 输出 16 位大写十六进制（同 CRT）。 */
static void zan__cf_vfmt(char *out, size_t cap, const char *fmt, va_list ap) {
    size_t o = 0;
    for (const char *p = fmt; *p && o < cap - 1; p++) {
        if (*p != '%') { out[o++] = *p; continue; }
        p++;
        if (*p == '%') { out[o++] = '%'; continue; }
        int ljust = 0, zero = 0;
        for (;; p++) {
            if (*p == '-') ljust = 1;
            else if (*p == '0') zero = 1;
            else break;
        }
        int width = 0;
        while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        int lcount = 0;
        while (*p == 'l') { lcount++; p++; }
        char num[24];
        const char *s = NULL;
        char cbuf[2];
        unsigned long long uv = 0;
        int neg = 0;
        switch (*p) {
        case 's':
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            break;
        case 'c':
            cbuf[0] = (char)va_arg(ap, int);
            cbuf[1] = '\0';
            s = cbuf;
            break;
        case 'd': case 'i': {
            long long v = lcount >= 2 ? va_arg(ap, long long)
                        : lcount == 1 ? va_arg(ap, long)
                                      : va_arg(ap, int);
            if (v < 0) { neg = 1; uv = (unsigned long long)(-(v + 1)) + 1; }
            else uv = (unsigned long long)v;
            s = zan__cf_dec_r(num + sizeof num, uv);
            break;
        }
        case 'u': case 'x': case 'X':
            uv = lcount >= 2 ? va_arg(ap, unsigned long long)
               : lcount == 1 ? va_arg(ap, unsigned long)
                             : va_arg(ap, unsigned int);
            s = (*p == 'u') ? zan__cf_dec_r(num + sizeof num, uv)
                            : zan__cf_hex_r(num + sizeof num, uv, *p == 'X');
            break;
        case 'p':
            uv = (unsigned long long)(uintptr_t)va_arg(ap, void *);
            s = zan__cf_hex_r(num + sizeof num, uv, 1);
            width = 16;
            zero = 1;
            ljust = 0;
            break;
        default:
            out[o++] = '%';
            if (*p && o < cap - 1) out[o++] = *p;
            continue;
        }
        size_t len = strlen(s);
        size_t body = len + (size_t)neg;
        size_t pad = (size_t)width > body ? (size_t)width - body : 0;
        char pc = (zero && !ljust) ? '0' : ' ';
        if (!ljust) {
            if (pc == '0' && neg && o < cap - 1) { out[o++] = '-'; neg = 0; }
            while (pad-- && o < cap - 1) out[o++] = pc;
        }
        if (neg && o < cap - 1) out[o++] = '-';
        while (*s && o < cap - 1) out[o++] = *s++;
        if (ljust) while (pad-- && o < cap - 1) out[o++] = ' ';
    }
    out[o] = '\0';
}

static void zan__cf_printf(zan__cf_t f, const char *fmt, ...) {
    char out[1024];
    va_list ap;
    va_start(ap, fmt);
    zan__cf_vfmt(out, sizeof out, fmt, ap);
    va_end(ap);
    zan__cf_puts(f, out);
}

/* 崩溃路径内的路径/标签拼装也走同一格式化器（绕开 CRT snprintf） */
static int zan__crash_snprintf(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    zan__cf_vfmt(buf, cap, fmt, ap);
    va_end(ap);
    return (int)strlen(buf);
}

/* 追加模式打开崩溃日志；失败返回 h==NULL 的句柄，便于沿用 !f 判空 */
static zan__cf_t zan__cf_open_append(const char *path) {
    zan__cf_t f;
    f.h = CreateFileA(path, FILE_APPEND_DATA,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    if (f.h == INVALID_HANDLE_VALUE) f.h = NULL;
    return f;
}
static void zan__cf_close(zan__cf_t f) {
    if (f.h) CloseHandle(f.h);
}
