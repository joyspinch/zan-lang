/* rt_sigpipe_test.c -- writing to a hung-up peer must not kill the process.
 *
 * A browser closing a Server-Sent Events stream (or any client walking away
 * mid-response) leaves the server writing into a socket whose peer is gone.
 * On POSIX that raises SIGPIPE, whose default action terminates the process:
 * one closed monitor tab used to take the whole web server down, silently and
 * with no log line. The reactor therefore neutralises the signal, and every
 * send reports a dead peer through its return value instead.
 */
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "../../src/runtime/rt_io.h"

static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL: %s\n", what); fails++; }
    else       { printf("ok: %s\n", what); }
}

int main(void) {
    int sv[2];
    char buf[4096];
    int64_t r;
    struct sigaction cur;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        printf("FAIL: socketpair\n");
        return 1;
    }
    zan_io_init();

    memset(&cur, 0, sizeof(cur));
    check(sigaction(SIGPIPE, NULL, &cur) == 0 && cur.sa_handler != SIG_DFL,
          "zan_io_init() takes SIGPIPE off its default (fatal) action");

    close(sv[1]);                      /* the peer hangs up */
    memset(buf, 'x', sizeof(buf));

    /* Without the fix the first or second send raises SIGPIPE and this process
     * dies here: no output past this point, and ctest reports a signal. */
    r = zan_io_socket_send((intptr_t)sv[0], buf, (int64_t)sizeof(buf), 0);
    if (r >= 0) {
        r = zan_io_socket_send((intptr_t)sv[0], buf, (int64_t)sizeof(buf), 0);
    }
    check(r == -2, "send to a hung-up peer returns -2 (fatal), not a signal");

    printf("still alive after writing to a closed peer\n");
    close(sv[0]);
    return fails ? 1 : 0;
}
