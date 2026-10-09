/* 底层系统交互与数据协议契约 */
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

    close(sv[1]);                      /* 核心系统底层抽象与内存语义契约 */
    memset(buf, 'x', sizeof(buf));

    /* 底层系统交互与数据协议契约 */
    r = zan_io_socket_send((intptr_t)sv[0], buf, (int64_t)sizeof(buf), 0);
    if (r >= 0) {
        r = zan_io_socket_send((intptr_t)sv[0], buf, (int64_t)sizeof(buf), 0);
    }
    check(r == -2, "send to a hung-up peer returns -2 (fatal), not a signal");

    printf("still alive after writing to a closed peer\n");
    close(sv[0]);
    return fails ? 1 : 0;
}
