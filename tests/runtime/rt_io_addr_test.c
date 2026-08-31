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

int main(void) {
    struct sockaddr_in v4;
    struct sockaddr_in6 v6;
    unsigned char records[ZAN_IO_SA_STRIDE * 8];
    memset(&v4, 0, sizeof(v4));
    memset(&v6, 0, sizeof(v6));
    v4.sin_family = AF_INET;
    v6.sin6_family = AF_INET6;
    CHECK(zan_io_sockaddr_family(&v4, (int32_t)sizeof(v4)) == AF_INET);
    CHECK(zan_io_sockaddr_family(&v6, (int32_t)sizeof(v6)) == AF_INET6);
    CHECK(zan_io_sockaddr_family(&v4, 2) == 0);
    CHECK(zan_io_sockaddr_family(&v6, 16) == 0);
    CHECK(zan_io_sockaddr_family(NULL, 28) == 0);
    CHECK(zan_io_resolve_all("127.0.0.1", 80, records, sizeof(records)) >= 1);
    CHECK(zan_io_sockaddr_family(records, 16) == AF_INET);
    CHECK(zan_io_resolve_all("::1", 80, records, sizeof(records)) >= 1);
    CHECK(zan_io_sockaddr_family(records, 28) == AF_INET6);
    CHECK(zan_io_resolve_all("definitely.invalid.zan", 80,
                             records, sizeof(records)) == 0);
    CHECK(zan_io_resolve_all("127.0.0.1", 80, records, 31) == 0);
    puts(fail ? "FAIL" : "PASS");
    return fail;
}
