/* rt_sync_net.c: 跨平台网卡接口枚举与原生 ICMP Ping 探测实现 */

#ifndef _WIN32
#define ZAN_PLAT_TEXT_MAX 65536
static pthread_key_t zan_plat_text_key;

static void zan_plat_text_dtor(void *buffer) { free(buffer); }

static void zan_plat_text_key_create(void) {
    if (pthread_key_create(&zan_plat_text_key, zan_plat_text_dtor) != 0)
        zan_rt_fatal("oom", "sync: text TLS init failed");
}

static char *zan_get_plat_text(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, zan_plat_text_key_create);
    char *buffer = (char *)pthread_getspecific(zan_plat_text_key);
    if (!buffer) {
        buffer = (char *)calloc(ZAN_PLAT_TEXT_MAX, 1);
        if (!buffer) zan_rt_fatal("oom", "sync: text TLS alloc failed");
        if (pthread_setspecific(zan_plat_text_key, buffer) != 0) {
            free(buffer);
            zan_rt_fatal("oom", "sync: text TLS set failed");
        }
    }
    return buffer;
}
#endif

#ifndef _WIN32
static void zan_plat_mac_from_sockaddr(struct sockaddr *sa, char *out,
                                       size_t out_size) {
    out[0] = '\0';
    if (!sa) return;
    const unsigned char *bytes = NULL;
    int len = 0;
#if defined(__linux__)
    if (sa->sa_family != AF_PACKET) return;
    struct sockaddr_ll *ll = (struct sockaddr_ll *)sa;
    bytes = ll->sll_addr;
    len = ll->sll_halen;
#else
    if (sa->sa_family != AF_LINK) return;
    struct sockaddr_dl *dl = (struct sockaddr_dl *)sa;
    bytes = (const unsigned char *)LLADDR(dl);
    len = dl->sdl_alen;
#endif
    if (len <= 0 || len > 8) return;
    size_t used = 0;
    for (int i = 0; i < len && used + 3 < out_size; i++) {
        used += (size_t)snprintf(out + used, out_size - used, "%s%02X",
                                 i ? ":" : "", bytes[i]);
    }
}
#endif

const char *zan_plat_net_interfaces(void) {
#ifdef _WIN32
    return "";
#else
    char *zan_plat_text = zan_get_plat_text();
    zan_plat_text[0] = '\0';
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) return zan_plat_text;

    size_t used = 0;
    for (struct ifaddrs *it = list; it; it = it->ifa_next) {
        if (!it->ifa_name) continue;
        int seen = 0;
        for (struct ifaddrs *p = list; p != it; p = p->ifa_next) {
            if (p->ifa_name && strcmp(p->ifa_name, it->ifa_name) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen) continue;

        char mac[32];
        mac[0] = '\0';
        int up = (it->ifa_flags & IFF_UP) && (it->ifa_flags & IFF_RUNNING);
        char addrs[2048];
        size_t addr_used = 0;
        addrs[0] = '\0';
        for (struct ifaddrs *p = list; p; p = p->ifa_next) {
            if (!p->ifa_name || strcmp(p->ifa_name, it->ifa_name) != 0) continue;
            if (!p->ifa_addr) continue;
            if (!mac[0]) zan_plat_mac_from_sockaddr(p->ifa_addr, mac, sizeof mac);
            char text[INET6_ADDRSTRLEN];
            text[0] = '\0';
            if (p->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *v4 = (struct sockaddr_in *)p->ifa_addr;
                inet_ntop(AF_INET, &v4->sin_addr, text, sizeof text);
            } else if (p->ifa_addr->sa_family == AF_INET6) {
                struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)p->ifa_addr;
                inet_ntop(AF_INET6, &v6->sin6_addr, text, sizeof text);
            }
            if (!text[0]) continue;
            if (addr_used + strlen(text) + 2 >= sizeof addrs) continue;
            addr_used += (size_t)snprintf(addrs + addr_used,
                                          sizeof addrs - addr_used, "%s%s",
                                          addr_used ? "," : "", text);
        }

        unsigned index = if_nametoindex(it->ifa_name);
        int written = snprintf(zan_plat_text + used, ZAN_PLAT_TEXT_MAX - used,
                               "%s\t%u\t%d\t%s\t%s\n", it->ifa_name, index,
                               up ? 1 : 0, mac, addrs);
        if (written < 0 || (size_t)written >= ZAN_PLAT_TEXT_MAX - used) break;
        used += (size_t)written;
    }
    freeifaddrs(list);
    return zan_plat_text;
#endif
}

#ifndef _WIN32
static uint16_t zan_plat_icmp_checksum(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint32_t)((bytes[0] << 8) | bytes[1]);
        bytes += 2;
        len -= 2;
    }
    if (len) sum += (uint32_t)(bytes[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static long long zan_plat_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}
#endif

int32_t zan_plat_icmp_ping(const char *address, int32_t timeout_ms) {
#ifdef _WIN32
    (void)address;
    (void)timeout_ms;
    return -3;
#else
    if (!address || !address[0]) return -4;
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof dst);
    dst.sin_family = AF_INET;
    if (inet_pton(AF_INET, address, &dst.sin_addr) != 1) return -4;
    if (timeout_ms < 0) timeout_ms = 0;

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    int datagram = fd >= 0;
    if (fd < 0) fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) return -3;

    unsigned char packet[16];
    memset(packet, 0, sizeof packet);
    packet[0] = 8;                                  /* ICMP_ECHO */
    uint16_t ident = (uint16_t)(getpid() & 0xFFFF);
    packet[4] = (unsigned char)(ident >> 8);
    packet[5] = (unsigned char)(ident & 0xFF);
    packet[6] = 0;
    packet[7] = 1;                                  /* sequence */
    memcpy(packet + 8, "zan-ping", 8);
    uint16_t sum = zan_plat_icmp_checksum(packet, sizeof packet);
    packet[2] = (unsigned char)(sum >> 8);
    packet[3] = (unsigned char)(sum & 0xFF);

    long long start = zan_plat_now_us();
    if (sendto(fd, packet, sizeof packet, 0, (struct sockaddr *)&dst,
               sizeof dst) < 0) {
        int err = errno;
        close(fd);
        if (err == EHOSTUNREACH || err == ENETUNREACH) return -2;
        return -3;
    }

    for (;;) {
        long long elapsed_ms = (zan_plat_now_us() - start) / 1000;
        int remain = (int)((long long)timeout_ms - elapsed_ms);
        if (remain <= 0) {
            close(fd);
            return -1;
        }
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int ready = poll(&pfd, 1, remain);
        if (ready == 0) {
            close(fd);
            return -1;
        }
        if (ready < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -3;
        }

        unsigned char reply[1024];
        struct sockaddr_in from;
        socklen_t from_len = sizeof from;
        ssize_t got = recvfrom(fd, reply, sizeof reply, 0,
                               (struct sockaddr *)&from, &from_len);
        if (got < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -3;
        }
        size_t offset = 0;
        if (!datagram) {
            if (got < 20) continue;
            offset = (size_t)((reply[0] & 0x0F) * 4);
            if ((size_t)got < offset + 8) continue;
        } else if (got < 8) {
            continue;
        }
        unsigned type = reply[offset];
        if (type == 0) {                            /* ICMP_ECHOREPLY */
            long long rtt_us = zan_plat_now_us() - start;
            close(fd);
            long long rtt_ms = rtt_us / 1000;
            return (int32_t)(rtt_ms > 0x7FFFFFFF ? 0x7FFFFFFF : rtt_ms);
        }
        if (type == 3) {
            close(fd);
            return -2;
        }
        if (type == 11) {
            close(fd);
            return -2;
        }
    }
#endif
}
