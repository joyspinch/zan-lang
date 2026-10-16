/* b43: syscall census dylib for UE-wedged native fixtures (macOS only).
 *
 * Usage:  cc -dynamiclib -o /tmp/census.dylib crt-transition/interpose_census.c
 *         DYLD_INSERT_LIBRARIES=/tmp/census.dylib <fixture-binary>
 *
 * Logs the first 60 calls per wrapped syscall (send/recv/select/poll/accept)
 * to /tmp/zan-census.log, then passes everything through untouched. Built to
 * dissect the redis_client darwin wedge (b43): the trace showed the reactor
 * faces behaving (connect probe -> send -> eager recv probe -> park) while
 * the scheduler spun without syscalls — i.e. an async this-capture bug in
 * the seed, not the runtime. The wedged process is un-killable (kernel
 * uninterruptible wait): run under a timeout and let the reboot reap it.
 * Change LOGPATH per investigation or runs will clobber each other. */
#define LOGPATH "/tmp/zan-census.log"
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <stdio.h>
#include <unistd.h>
static FILE *lg;
static void loginit(void) {
    if (!lg) lg = fopen(LOGPATH, "w");
}
static ssize_t (*r_send)(int, const void *, size_t, int);
static ssize_t my_send(int fd, const void *b, size_t l, int f) {
    loginit();
    ssize_t rv = r_send ? r_send(fd, b, l, f) : send(fd, b, l, f);
    static long n = 0;
    if (n < 60) { fprintf(lg, "send fd=%d len=%zu -> %zd\n", fd, l, rv); fflush(lg); }
    n++;
    return rv;
}
static ssize_t (*r_recv)(int, void *, size_t, int);
static ssize_t my_recv(int fd, void *b, size_t l, int f) {
    loginit();
    ssize_t rv = r_recv ? r_recv(fd, b, l, f) : recv(fd, b, l, f);
    static long n = 0;
    if (n < 60) { fprintf(lg, "recv fd=%d len=%zu -> %zd\n", fd, l, rv); fflush(lg); }
    n++;
    return rv;
}
static int (*r_select)(int, fd_set *, fd_set *, fd_set *, struct timeval *);
static int my_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t) {
    loginit();
    int rv = r_select ? r_select(n, r, w, e, t) : select(n, r, w, e, t);
    static long n2 = 0;
    if (n2 < 60) { fprintf(lg, "select n=%d -> %d\n", n, rv); fflush(lg); }
    n2++;
    return rv;
}
static int (*r_poll)(struct pollfd *, unsigned, int);
static int my_poll(struct pollfd *fds, unsigned n, int t) {
    loginit();
    int rv = r_poll ? r_poll(fds, n, t) : poll(fds, n, t);
    static long n2 = 0;
    if (n2 < 60) { fprintf(lg, "poll n=%u -> %d\n", n, rv); fflush(lg); }
    n2++;
    return rv;
}
__attribute__((used)) static struct { void *repl; void *orig; }
    inter[] __attribute__((section("__DATA,__interpose"))) = {
        { (void*)my_send,   (void*)send   },
        { (void*)my_recv,   (void*)recv   },
        { (void*)my_select, (void*)select },
        { (void*)my_poll,   (void*)poll   },
    };
