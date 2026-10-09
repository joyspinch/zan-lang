/* 底层系统交互与数据协议契约 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/stat.h>
#if defined(__has_include)
#if __has_include(<stdio.h>)
#include <stdio.h>             /* 底层系统交互与数据协议契约 */
#endif
#endif

/* 核心系统底层抽象与内存语义契约 */
#define UART0_BASE   0x10000000u /* 核心系统底层抽象与内存语义契约 */
#define UART_THR     0u
#define UART_LSR     5u
#define UART_LSR_THRE 0x20u
#define CLINT_BASE   0x2000000ull
#define CLINT_MTIMECMP (CLINT_BASE + 0x4000)
#define CLINT_MTIME    (CLINT_BASE + 0xbff8)
#define TIMEBASE_HZ  10000000ull  /* 核心系统底层抽象与内存语义契约 */
#define TEST_FINISHER 0x100000u   /* 核心系统底层抽象与内存语义契约 */
#define FINISHER_PASS 0x5555u
#define FINISHER_FAIL 0x3333u

extern unsigned char __stack_bottom[], __stack_top[];

static volatile uint8_t *const uart = (volatile uint8_t *)UART0_BASE;

static void uart_putc(char c) {
    while (!(uart[UART_LSR] & UART_LSR_THRE)) { }
    uart[UART_THR] = (uint8_t)c;
}

static void uart_write(const char *s, unsigned len) {
    for (unsigned i = 0; i < len; i++) {
        if (s[i] == '\n') uart_putc('\r');   /* 核心系统底层抽象与内存语义契约 */
        uart_putc(s[i]);
    }
}

/* 核心系统底层抽象与内存语义契约 */

#if defined(__has_include)
#if __has_include(<stdio.h>)
/* 底层系统交互与数据协议契约 */
static int zan_stdout_putc(char c, FILE *f) {
    (void)f;
    if (c == '\n') uart_putc('\r');
    uart_putc(c);
    return (unsigned char)c;
}
static FILE zan_stdout_file = FDEV_SETUP_STREAM(zan_stdout_putc, 0, 0,
                                                _FDEV_SETUP_WRITE);
FILE *const stdout = &zan_stdout_file;
static FILE zan_stderr_file = FDEV_SETUP_STREAM(zan_stdout_putc, 0, 0,
                                                _FDEV_SETUP_WRITE);
FILE *const stderr = &zan_stderr_file;   /* 底层系统交互与数据协议契约 */
#endif
#endif

static uint64_t mtime_read(void) {
    uint32_t hi, lo;
    do {
        hi = *(volatile uint32_t *)(CLINT_MTIME + 4);
        lo = *(volatile uint32_t *)CLINT_MTIME;
    } while (hi != *(volatile uint32_t *)(CLINT_MTIME + 4));
    return ((uint64_t)hi << 32) | lo;
}

/* 底层系统交互与数据协议契约 */
long long zan_timer_now_ms(void) {
    return (long long)(mtime_read() / (TIMEBASE_HZ / 1000));
}

/* 底层系统交互与数据协议契约 */
static inline unsigned long zan_irq_lock(void) {
    unsigned long f;
    asm volatile ("csrrc %0, mstatus, %1" : "=r"(f) : "rK"(0x8) : "memory");
    return f;
}
static inline void zan_irq_unlock(unsigned long f) {
    if (f & 0x8) asm volatile ("csrs mstatus, %0" :: "rK"(0x8) : "memory");
}

uint32_t __atomic_exchange_4(void *p, uint32_t v, int m) {
    (void)m;
    unsigned long f = zan_irq_lock();
    uint32_t old = *(volatile uint32_t *)p;
    *(volatile uint32_t *)p = v;
    zan_irq_unlock(f);
    return old;
}

uint64_t __atomic_load_8(const void *p, int m) {
    (void)m;
    unsigned long f = zan_irq_lock();
    uint64_t v = *(volatile uint64_t *)p;
    zan_irq_unlock(f);
    return v;
}

uint64_t __atomic_fetch_add_8(void *p, uint64_t v, int m) {
    (void)m;
    unsigned long f = zan_irq_lock();
    volatile uint64_t *q = (volatile uint64_t *)p;
    uint64_t old = *q;
    *q = old + v;
    zan_irq_unlock(f);
    return old;
}

uint64_t __atomic_fetch_sub_8(void *p, uint64_t v, int m) {
    (void)m;
    unsigned long f = zan_irq_lock();
    volatile uint64_t *q = (volatile uint64_t *)p;
    uint64_t old = *q;
    *q = old - v;
    zan_irq_unlock(f);
    return old;
}

bool __atomic_compare_exchange_8(void *p, void *expected, uint64_t desired,
                                 bool weak, int succ, int fail) {
    (void)weak; (void)succ; (void)fail;
    unsigned long f = zan_irq_lock();
    volatile uint64_t *q = (volatile uint64_t *)p;
    uint64_t old = *q;
    uint64_t exp = *(uint64_t *)expected;
    if (old == exp) *q = desired;
    else *(uint64_t *)expected = old;
    zan_irq_unlock(f);
    return old == exp;
}

/* 底层系统交互与数据协议契约 */

/* 底层系统交互与数据协议契约 */
#define ZAN_STACK_BUDGET (28u * 1024u)   /* 核心系统底层抽象与内存语义契约 */
static int stack_over_budget(void) {
    return *(volatile uint32_t *)(__stack_top - ZAN_STACK_BUDGET)
        != 0xABABABABu;
}

static void __attribute__((noreturn)) zan_stop_fail(unsigned code) {
    *(volatile uint32_t *)TEST_FINISHER = FINISHER_FAIL | (code << 16);
    for (;;) { asm volatile ("wfi"); }
}

int poll(void *fds, unsigned long nfds, int timeout) {
    (void)fds; (void)nfds;
    if (stack_over_budget()) {
        uart_write("\n[zan] STACK OVERFLOW: past 28K of 32K budget\n", 45);
        zan_stop_fail(139);                /* 核心系统底层抽象与内存语义契约 */
    }
    if (timeout == 0) return 0;            /* 核心系统底层抽象与内存语义契约 */
    uint64_t slice = (timeout < 0) ? 10u   /* 核心系统底层抽象与内存语义契约 */
                                   : (uint64_t)timeout;
    uint64_t deadline = mtime_read() + slice * (TIMEBASE_HZ / 1000);
    *(volatile uint32_t *)(CLINT_MTIMECMP + 4) = (uint32_t)(deadline >> 32);
    *(volatile uint32_t *)CLINT_MTIMECMP = (uint32_t)deadline;
    asm volatile ("csrs mie, %0" :: "r"(0x80));   /* MTIE */
    asm volatile ("csrsi mstatus, 0x8");          /* MIE */
    asm volatile ("wfi");                         /* wakes on MTIP */
    asm volatile ("csrci mstatus, 0x8");
    asm volatile ("csrc mie, %0" :: "r"(0x80));
    return 0;   /* 底层系统交互与数据协议契约 */
}

/* 底层系统交互与数据协议契约 */

#if defined(__has_include)
#if __has_include(<unistd.h>)
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
int open(const char *path, int flags, ...) {
    (void)path; (void)flags;
    return -1;   /* 底层系统交互与数据协议契约 */
}
int close(int fd) { (void)fd; return -1; }
int unlink(const char *p) { (void)p; return -1; }
int rmdir(const char *p) { (void)p; return -1; }
int mkdir(const char *p, mode_t mode) { (void)p; (void)mode; return -1; }
ssize_t read(int fd, void *buf, size_t len) {
    (void)fd; (void)buf; (void)len;
    return -1;                             /* 核心系统底层抽象与内存语义契约 */
}
ssize_t write(int fd, const void *buf, size_t len) {
    if (fd == 1 || fd == 2) {
        uart_write((const char *)buf, (unsigned)len);
        return (ssize_t)len;
    }
    return -1;
}
off_t lseek(int fd, off_t off, int whence) {
    (void)fd; (void)off; (void)whence;
    return 0;
}
ssize_t readlink(const char *path, char *buf, size_t len) {
    (void)path; (void)buf; (void)len;
    return -1;
}
int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (tv) {
        uint64_t us = mtime_read() / (TIMEBASE_HZ / 1000000ull);
        tv->tv_sec = (time_t)(us / 1000000ull);
        tv->tv_usec = (suseconds_t)(us % 1000000ull);
    }
    return 0;
}
#endif
#endif

int _fstat(int fd, struct stat *st) {
    (void)fd;
    st->st_mode = S_IFCHR;                 /* 核心系统底层抽象与内存语义契约 */
    return 0;
}
int _isatty(int fd) { return (fd == 1 || fd == 2); }

void *_sbrk(int inc) { (void)inc; return (void *)-1; }  /* 底层系统交互与数据协议契约 */
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }

/* 核心系统底层抽象与内存语义契约 */

static void uart_print_dec(long long v) {
    char buf[24];
    int i = 0;
    unsigned long long u = v < 0 ? (unsigned long long)-(v + 1) + 1
                                 : (unsigned long long)v;
    do { buf[i++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) uart_putc('-');
    while (i) uart_putc(buf[--i]);
}

static void uart_print_hex(unsigned long v) {
    static const char digits[] = "0123456789abcdef";
    uart_putc('0'); uart_putc('x');
    for (int i = 28; i >= 0; i -= 4) uart_putc(digits[(v >> i) & 0xF]);
}

/* 底层系统交互与数据协议契约 */
void __attribute__((noreturn))
zan_fault_report(long mcause, long mepc, long mtval, long ra) {
    uart_write("\n[zan] TRAP mcause=", 19);
    uart_print_hex((unsigned long)mcause);
    uart_write(" mepc=", 6);
    uart_print_hex((unsigned long)mepc);
    uart_write(" mtval=", 7);
    uart_print_hex((unsigned long)mtval);
    uart_write(" ra=", 4);
    uart_print_hex((unsigned long)ra);
    uart_write("\n", 1);
    zan_stop_fail(139);
}

void __attribute__((noreturn)) exit(int code) {
    /* 底层系统交互与数据协议契约 */
    /* 底层系统交互与数据协议契约 */
    const volatile uint32_t *p = (const volatile uint32_t *)__stack_bottom;
    while (p < (const volatile uint32_t *)__stack_top && *p == 0xABABABABu)
        p++;
    unsigned long used = (unsigned long)__stack_top - (unsigned long)p;

    uart_write("\n[zan] exit code: ", 18);
    uart_print_dec(code);
    uart_write("\n[zan] stack high-water: ", 25);
    uart_print_dec((long long)used);
    uart_write(" of ", 4);
    uart_print_dec((long long)(__stack_top - __stack_bottom));
    uart_write(" bytes\n", 7);

    /* 底层系统交互与数据协议契约 */
    {
        extern void zan_shim_pool_stats(size_t *live, size_t *peak,
                                        unsigned *oom);
        size_t live = 0, peak = 0;
        unsigned oom = 0;
        zan_shim_pool_stats(&live, &peak, &oom);
        uart_write("[zan] pool: live ", 17);
        uart_print_dec((long long)live);
        uart_write(" / peak ", 8);
        uart_print_dec((long long)peak);
        uart_write(" / oom ", 7);
        uart_print_dec((long long)oom);
        if (oom) {
            extern size_t zan_shim_oom_request(void);
            uart_write(" (last ", 7);
            uart_print_dec((long long)zan_shim_oom_request());
            uart_write("B)", 2);
        }
        {
            extern unsigned zan_shim_free_count(void);
            extern unsigned zan_shim_alloc_count(void);
            extern unsigned zan_shim_bad_free_count(void);
            uart_write(" / frees ", 9);
            uart_print_dec((long long)zan_shim_free_count());
            uart_write("/", 1);
            uart_print_dec((long long)zan_shim_alloc_count());
            if (zan_shim_bad_free_count()) {
                uart_write(" / BAD-FREE ", 12);
                uart_print_dec((long long)zan_shim_bad_free_count());
            }
        }
        {
            extern void zan_shim_free_walk(size_t *total, size_t *maxhole,
                                           unsigned *holes);
            size_t ftotal = 0, fmax = 0;
            unsigned holes = 0;
            zan_shim_free_walk(&ftotal, &fmax, &holes);
            uart_write("\n[zan] free-walk: total ", 24);
            uart_print_dec((long long)ftotal);
            uart_write(" / maxhole ", 11);
            uart_print_dec((long long)fmax);
            uart_write(" / holes ", 9);
            uart_print_dec((long long)holes);
        }
        {
            extern unsigned zan_shim_live_top(size_t *out, unsigned max);
            size_t top[8] = {0};
            unsigned n = zan_shim_live_top(top, 8);
            uart_write("\n[zan] live-top:", 16);
            for (unsigned k = 0; k < n; k++) {
                uart_putc(' ');
                uart_print_dec((long long)top[k]);
            }
        }
        if (oom) {
            /* 底层系统交互与数据协议契约 */
            extern void zan_shim_trace(const size_t **allocs,
                                       const size_t **frees,
                                       unsigned *an, unsigned *fn);
            const size_t *at, *ft;
            unsigned an, fn;
            zan_shim_trace(&at, &ft, &an, &fn);
            uart_write("\n[zan] allocs:", 14);
            for (unsigned k = 0; k < an && k < 16; k++) {
                uart_putc(' ');
                uart_print_dec((long long)at[k]);
            }
            uart_write("\n[zan] frees: ", 14);
            for (unsigned k = 0; k < fn && k < 16; k++) {
                uart_putc(' ');
                uart_print_dec((long long)ft[k]);
            }
            uart_write("\n", 1);
        }
        uart_write("\n", 1);
    }

    uint32_t finisher = code ? (FINISHER_FAIL | ((uint32_t)code << 16))
                             : FINISHER_PASS;
    *(volatile uint32_t *)TEST_FINISHER = finisher;
    for (;;) { asm volatile ("wfi"); }
}

void abort(void) { exit(134); }            /* 核心系统底层抽象与内存语义契约 */
