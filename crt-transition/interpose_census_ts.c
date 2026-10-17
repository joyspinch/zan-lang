/* b44 (timestamped variant of interpose_census.c): connect/select/send/accept census. */
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
static FILE *lg;
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
static void loginit(void) {
    if (!lg) lg = fopen("/tmp/zan-census-ts.log", "w");
}
static int (*r_connect)(int, const struct sockaddr *, socklen_t);
static int my_connect(int fd, const struct sockaddr *a, socklen_t l) {
    loginit();
    int rv = r_connect ? r_connect(fd, a, l) : connect(fd, a, l);
    fprintf(lg, "[%10.2f] connect fd=%d -> %d\n", now_ms(), fd, rv);
    fflush(lg);
    return rv;
}
static int (*r_select)(int, fd_set *, fd_set *, fd_set *, struct timeval *);
static int my_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t) {
    loginit();
    double t0 = now_ms();
    int wfd = -1, i;
    if (w) { for (i = 0; i < n; i++) if (FD_ISSET(i, w)) { wfd = i; break; } }
    int rv = r_select ? r_select(n, r, w, e, t) : select(n, r, w, e, t);
    static long n2 = 0;
    if (n2 < 120) {
        fprintf(lg, "[%10.2f] select wfd=%d tmo=%d -> %d (%.2f ms)\n",
                t0, wfd, t ? (int)t->tv_sec : -1, rv, now_ms() - t0);
        fflush(lg);
    }
    n2++;
    return rv;
}
static ssize_t (*r_send)(int, const void *, size_t, int);
static ssize_t my_send(int fd, const void *b, size_t l, int f) {
    loginit();
    ssize_t rv = r_send ? r_send(fd, b, l, f) : send(fd, b, l, f);
    static long n3 = 0;
    if (n3 < 40) { fprintf(lg, "[%10.2f] send fd=%d len=%zu -> %zd\n", now_ms(), fd, l, rv); fflush(lg); }
    n3++;
    return rv;
}
static int (*r_accept)(int, struct sockaddr *, socklen_t *);
static int my_accept(int fd, struct sockaddr *a, socklen_t *l) {
    loginit();
    int rv = r_accept ? r_accept(fd, a, l) : accept(fd, a, l);
    static long n4 = 0;
    if (n4 < 20) { fprintf(lg, "[%10.2f] accept lfd=%d -> %d\n", now_ms(), fd, rv); fflush(lg); }
    n4++;
    return rv;
}
__attribute__((used)) static struct { void *repl; void *orig; }
    inter[] __attribute__((section("__DATA,__interpose"))) = {
        { (void*)my_connect, (void*)connect },
        { (void*)my_select,  (void*)select  },
        { (void*)my_send,    (void*)send    },
        { (void*)my_accept,  (void*)accept  },
    };
