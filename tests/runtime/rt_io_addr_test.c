/* Binary sockaddr classification and all-address resolution tests.
 * No public-network dependency: literals and localhost only. */
#include "src/runtime/rt_io.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

static int fail;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL: %s\n", #x); fail = 1; } } while (0)

static struct sockaddr_in v4_addr(const char *text) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    inet_pton(AF_INET, text, &a.sin_addr);
    return a;
}

static struct sockaddr_in6 v6_addr(const char *text) {
    struct sockaddr_in6 a;
    memset(&a, 0, sizeof(a));
    a.sin6_family = AF_INET6;
    inet_pton(AF_INET6, text, &a.sin6_addr);
    return a;
}

static void check_v4(const char *text, int allow_loopback, int expected) {
    struct sockaddr_in a = v4_addr(text);
    int got = zan_io_sockaddr_is_safe(&a, (int32_t)sizeof(a), allow_loopback);
    if (got != expected) {
        fprintf(stderr, "FAIL: v4 %s allow=%d got=%d want=%d\n",
                text, allow_loopback, got, expected);
        fail = 1;
    }
}

static void check_v6(const char *text, int allow_loopback, int expected) {
    struct sockaddr_in6 a = v6_addr(text);
    int got = zan_io_sockaddr_is_safe(&a, (int32_t)sizeof(a), allow_loopback);
    if (got != expected) {
        fprintf(stderr, "FAIL: v6 %s allow=%d got=%d want=%d\n",
                text, allow_loopback, got, expected);
        fail = 1;
    }
}

int main(void) {
    struct sockaddr_in v4;
    struct sockaddr_in6 v6;
    unsigned char records[ZAN_IO_SA_STRIDE * 8];
    zan_io_init();
    memset(&v4, 0, sizeof(v4));
    memset(&v6, 0, sizeof(v6));
    v4.sin_family = AF_INET;
    v6.sin6_family = AF_INET6;
    CHECK(zan_io_sockaddr_family(&v4, (int32_t)sizeof(v4)) == AF_INET);
    CHECK(zan_io_sockaddr_family(&v6, (int32_t)sizeof(v6)) == AF_INET6);
    CHECK(zan_io_sockaddr_family(&v4, 2) == 0);
    CHECK(zan_io_sockaddr_family(&v6, 16) == 0);
    CHECK(zan_io_sockaddr_family(NULL, 28) == 0);

    /* IPv4 binary CIDR table: every dangerous block must reject regardless of
     * textual spelling, loopback honours the explicit exception only. */
    check_v4("127.0.0.1", 0, 0);
    check_v4("127.0.0.1", 1, 1);
    check_v4("0.1.2.3", 1, 0);
    check_v4("10.1.2.3", 1, 0);
    check_v4("169.254.1.2", 1, 0);
    check_v4("172.16.0.1", 1, 0);
    check_v4("172.31.255.254", 1, 0);
    check_v4("192.168.1.1", 1, 0);
    check_v4("192.0.0.1", 1, 0);
    check_v4("192.0.2.1", 1, 0);
    check_v4("192.88.99.1", 1, 0);
    check_v4("198.18.0.1", 1, 0);
    check_v4("198.51.100.1", 1, 0);
    check_v4("203.0.113.1", 1, 0);
    check_v4("100.64.0.1", 1, 0);
    check_v4("224.0.0.1", 1, 0);
    check_v4("240.0.0.1", 1, 0);
    check_v4("255.255.255.255", 1, 0);
    check_v4("8.8.8.8", 1, 1);

    /* IPv6: unspecified, loopback, ULA, link-local, multicast, documentation. */
    check_v6("::", 1, 0);
    check_v6("::1", 0, 0);
    check_v6("::1", 1, 1);
    check_v6("fc00::1", 1, 0);
    check_v6("fd12:3456::1", 1, 0);
    check_v6("fe80::1", 1, 0);
    check_v6("ff02::1", 1, 0);
    check_v6("2001:db8::1", 1, 0);
    check_v6("2001:4860:4860::8888", 1, 1);

    /* IPv4-mapped IPv6 must re-classify under the IPv4 table. */
    check_v6("::ffff:127.0.0.1", 0, 0);
    check_v6("::ffff:127.0.0.1", 1, 1);
    check_v6("::ffff:10.0.0.1", 1, 0);
    check_v6("::ffff:8.8.8.8", 1, 1);

    /* Resolver: full chain, stable records, stride padding zeroed. The
     * 0xa5 poison proves resolve_all never leaves stale caller bytes. */
    memset(records, 0xa5, sizeof(records));
    int n = (int)zan_io_resolve_all("127.0.0.1", 80, records, sizeof(records));
    if (n < 1) {
        fprintf(stderr, "FAIL: zan_io_resolve_all(\"127.0.0.1\", 80, "
                        "records, sizeof(records)) >= 1 (n=%d)\n", n);
        fail = 1;
    } else {
        CHECK(zan_io_sockaddr_family(records, 16) == AF_INET);
        CHECK(records[16] == 0);
        CHECK(records[31] == 0);
    }
    memset(records, 0xa5, sizeof(records));
    n = (int)zan_io_resolve_all("::1", 80, records, sizeof(records));
    if (n < 1) {
        fprintf(stderr, "FAIL: zan_io_resolve_all(\"::1\", 80, "
                        "records, sizeof(records)) >= 1 (n=%d)\n", n);
        fail = 1;
    } else {
        CHECK(zan_io_sockaddr_family(records, 28) == AF_INET6);
        CHECK(records[28] == 0);
        CHECK(records[31] == 0);
    }
    memset(records, 0xa5, sizeof(records));
    CHECK(zan_io_resolve_all("", 80, records, sizeof(records)) == 0);
    CHECK(records[0] == 0);
    memset(records, 0xa5, sizeof(records));
    CHECK(zan_io_resolve_all("127.0.0.1", 80, records, 31) == 0);
    CHECK(records[0] == 0);
    zan_io_shutdown();
    puts(fail ? "FAIL" : "PASS");
    return fail;
}
