/* Transitional host externs (RT_OBJS second object): the zan_file_* /
 * zan_embed_* / zan_mmap_* / zan_pkg_fopen family declared by the pulled
 * stdlib, implemented over libc. The compiler binary itself only exercises
 * zan_pkg_fopen + libc (ReadAllText/WriteAllText/ListNames); the rest of the
 * family exists so pulled stdlib method bodies link. Programs compiled BY
 * this compiler additionally reference the zan_ext / zan_dt / zan_dir helper
 * families (see ngen_host.zan) — those live in the full runtime objects, not
 * here. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <time.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/* Batch 3 (package/embed, fopen/remove/rename, read_path, FileStream,
 * FileInfo, TryLock/Unlock, mmap) moved to runtime_core.zan and is
 * compiled out here under -DZAN_RT_CORE_ZAN. The zan_plat_* network half
 * at the bottom stays C in both configurations. */
#ifndef ZAN_RT_CORE_ZAN

static FILE *h2f(long h) { return (FILE *)(size_t)h; }

/* ---- package + embed: zanc embeds nothing, local files always win ----
 * Weak definitions: a gen0-style compiler bakes resources and defines its
 * own zan_embed_* into the program object (embedres.c), and the program's
 * strong definitions must win the link without a duplicate-symbol error. */
FILE *zan_pkg_fopen(const char *path, const char *mode) { return fopen(path, mode); }
__attribute__((weak)) int zan_embed_has(const char *name) { (void)name; return 0; }
__attribute__((weak)) const char *zan_embed_read(const char *name) { (void)name; return NULL; }
__attribute__((weak)) void *zan_embed_bytes(const char *name, int *outLen) { (void)name; if (outLen) *outLen = 0; return NULL; }
__attribute__((weak)) const char *zan_embed_list(const char *prefix) { (void)prefix; return ""; }

/* ---- File.zan fopen/remove/rename (DllImport EntryPoint remaps) ---- */
FILE *zan_file_fopen(const char *path, const char *mode) { return fopen(path, mode); }
int zan_file_remove(const char *path) { return remove(path); }
int zan_file_rename(const char *oldp, const char *newp) { return rename(oldp, newp); }

/* ---- Directory.zan read-path resolution, matching rt_file.c semantics:
 * NOT realpath. A relative path that exists in the working directory
 * resolves to "" (the caller then keeps its own spelling); a missing
 * relative path falls back to the packaged copy under $ZAN_PKG_DIR or next
 * to the executable; absolute paths and misses return "". Returning the
 * argument itself is never allowed (the managed side would release an
 * unretained managed string). */
static long zanh_exists(const char *p) {
    struct stat st;
    return (p && p[0] && stat(p, &st) == 0) ? 0 : -1;
}

static const char *zanh_app_dir(void) {
    static char dir[4096];
    const char *env = getenv("ZAN_APP_DIR");
    if (env && env[0] && strlen(env) < sizeof(dir)) { return env; }
    char exe[4096];
    uint32_t cap = (uint32_t)sizeof(exe);
    dir[0] = '\0';
    if (_NSGetExecutablePath(exe, &cap) != 0) { return dir; }
    char *sep = strrchr(exe, '/');
    if (sep && sep != exe) {
        *sep = '\0';
        if (strlen(exe) < sizeof(dir)) { memcpy(dir, exe, strlen(exe) + 1); }
    }
    return dir;
}

char *zan_file_read_path(const char *path) {
    if (zanh_exists(path) >= 0) { return strdup(""); }
    if (path[0] != '/' && path[0] != '\\' && path[1] != ':') {
        for (int which = 0; which < 2; which++) {
            const char *base = which == 0 ? getenv("ZAN_PKG_DIR") : zanh_app_dir();
            if (!base || !base[0]) { continue; }
            char alt[4096];
            if (strlen(base) + strlen(path) + 2 > sizeof(alt)) { continue; }
            sprintf(alt, "%s/%s", base, path);
            if (zanh_exists(alt) >= 0) { return strdup(alt); }
        }
    }
    return strdup("");
}

/* ---- FileStream.zan: FILE*-backed handle, 0 = closed/failed ---- */
long zan_file_open(const char *path, const char *mode) {
    FILE *f = fopen(path, mode);
    return f ? (long)(size_t)f : 0;
}
long zan_file_read(long h, void *buf, long count) {
    if (!h) { return -1; }
    return (long)fread(buf, 1, (size_t)count, h2f(h));
}
long zan_file_write(long h, void *buf, long count) {
    if (!h) { return -1; }
    return (long)fwrite(buf, 1, (size_t)count, h2f(h));
}
long zan_file_seek(long h, long off, int origin) {
    if (!h) { return -1; }
    /* returns the resulting absolute offset (like the oracle's
     * fseeko!=0 ? -1 : ftello), not fseek's success flag -- FileStream.Length
     * reads the size as seek(handle, 0, SEEK_END) */
    if (fseek(h2f(h), (long)off, origin) != 0) { return -1; }
    return (long)ftell(h2f(h));
}
long zan_file_tell(long h) { return h ? (long)ftell(h2f(h)) : -1; }
long zan_file_flush(long h) { return h ? (long)fflush(h2f(h)) : -1; }
long zan_file_close(long h) { if (!h) { return 0; } return fclose(h2f(h)); }
long zan_file_eof(long h) { return h ? (feof(h2f(h)) ? 1 : 0) : 1; }

/* ---- FileInfo.zan / FileInfoEx.zan ----
 * attributes: bit0 readonly, bit1 hidden (leading '.'), bit2 directory;
 * -1 = path missing. time: 0 = mtime, 1 = creation (st_ctime on POSIX,
 * like the oracle), 2 = atime. length: -1 only when stat fails (the oracle
 * reports directory sizes too). */
long zan_file_length(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) { return -1; }
    return (long)st.st_size;
}
long zan_file_attributes(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) { return -1; }
    long a = 0;
    if (access(path, W_OK) != 0) { a |= 1; }
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (base[0] == '.') { a |= 2; }
    if ((st.st_mode & S_IFMT) == S_IFDIR) { a |= 4; }
    return a;
}
long zan_file_time(const char *path, int which) {
    /* oracle rt_file.c: missing/empty path -> 0 (not -1) */
    if (!path || !path[0]) { return 0; }
    struct stat st;
    if (stat(path, &st) != 0) { return 0; }
    if (which == 1) { return (long)st.st_ctime; }
    if (which == 2) { return (long)st.st_atime; }
    return (long)st.st_mtime;
}
long zan_file_set_readonly(const char *path, int on) {
    struct stat st;
    if (stat(path, &st) != 0) { return 0; }
    mode_t m = st.st_mode;
    if (on) { m &= ~(mode_t)0222; } else { m |= 0222; }
    return chmod(path, m) == 0 ? 1 : 0;
}
long zan_file_set_time(const char *path, int which, long unixSec) {
    struct stat st;
    if (stat(path, &st) != 0) { return 0; }
    struct timespec ts[2];
    ts[0] = st.st_atimespec;
    ts[1] = st.st_mtimespec;
    struct timespec want;
    want.tv_sec = (time_t)unixSec;
    want.tv_nsec = 0;
    if (which == 0) { ts[1] = want; }
    else if (which == 1) { return 0; }   /* birthtime is not settable */
    else { ts[0] = want; }
    return utimensat(AT_FDCWD, path, ts, 0) == 0 ? 1 : 0;
}

/* ---- File.zan TryLock/Unlock: handles encode (gen << 32) | slot into a
 * small table, like the oracle (rt_file.c): the generation makes a second
 * unlock of the same handle value fail (returns 0) instead of releasing
 * whatever lock now occupies the slot. ---- */
#define ZAN_LK_CAP 32
static struct { int fd; int used; unsigned int gen; } g_lk_table[ZAN_LK_CAP];

static long zan_lk_index(long long h) {
    if (h <= 0) { return -1; }
    unsigned long long u = (unsigned long long)h;
    unsigned int gen = (unsigned int)(u >> 32);
    unsigned long long slot = u & 0xFFFFFFFFULL;
    if (slot >= ZAN_LK_CAP) { return -1; }
    if (!g_lk_table[slot].used || g_lk_table[slot].gen != gen) { return -1; }
    return (long)slot;
}

long zan_file_try_lock(const char *path) {
    if (!path || !path[0]) { return 0; }
    long slot = -1;
    for (long i = 0; i < ZAN_LK_CAP; i++) {
        if (!g_lk_table[i].used) { slot = i; g_lk_table[i].used = 1; break; }
    }
    if (slot < 0) { return 0; }
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) { g_lk_table[slot].used = 0; return 0; }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); g_lk_table[slot].used = 0; return 0; }
    unsigned int next = g_lk_table[slot].gen + 1;
    if (next == 0) {  /* wrap: retire the slot like the oracle */
        close(fd);
        g_lk_table[slot].used = 2;
        return 0;
    }
    g_lk_table[slot].gen = next;
    g_lk_table[slot].fd = fd;
    return (long)(((unsigned long long)next << 32) | (unsigned long long)slot);
}

long zan_file_unlock(long long h) {
    long slot = zan_lk_index(h);
    if (slot < 0) { return 0; }
    int fd = g_lk_table[slot].fd;
    g_lk_table[slot].fd = -1;
    g_lk_table[slot].used = 0;
    unsigned int ng = g_lk_table[slot].gen + 1;
    if (ng == 0) { ng = 1; }
    g_lk_table[slot].gen = ng;   /* a second unlock of the same value fails */
    close(fd);                   /* drops the flock */
    return 1;
}

/* ---- MemoryMappedFile.zan: nothing in the bootstrap exercises mmap ---- */
long zan_mmap_create(const char *n, long s) { (void)n; (void)s; return 0; }
long zan_mmap_open(const char *n, long s) { (void)n; (void)s; return 0; }
long zan_mmap_from_file(const char *p, long s) { (void)p; (void)s; return 0; }
long zan_mmap_map(long h, long s) { (void)h; (void)s; return 0; }
long zan_mmap_unmap(long p, long s) { (void)p; (void)s; return 0; }
long zan_mmap_flush(long p, long s) { (void)p; (void)s; return 0; }
long zan_mmap_close(long h) { (void)h; return 0; }
long zan_mmap_unlink(const char *n) { (void)n; return 0; }
#endif /* ZAN_RT_CORE_ZAN */

/* ---- Network.zan: adapter snapshot + ICMP echo, ports of rt_sync.c.
 * Batch 4: moved to runtime_core.zan; under -DZAN_RT_CORE_ZAN this whole
 * file compiles out (the C side remains for the A/B baseline build). ---- */
#ifndef ZAN_RT_CORE_ZAN
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <ifaddrs.h>
#include <poll.h>
#include <errno.h>

#define ZAN_PLAT_TEXT_MAX 65536
static char zan_net_text[ZAN_PLAT_TEXT_MAX];

static void zan_mac_from_sockaddr(struct sockaddr *sa, char *out, unsigned out_size) {
    out[0] = '\0';
    if (!sa) return;
    if (sa->sa_family != AF_LINK) return;
    struct sockaddr_dl *dl = (struct sockaddr_dl *)sa;
    const unsigned char *bytes = (const unsigned char *)LLADDR(dl);
    int len = dl->sdl_alen;
    if (len <= 0 || len > 8) return;
    unsigned used = 0;
    for (int i = 0; i < len && used + 3 < out_size; i++) {
        used += (unsigned)snprintf(out + used, out_size - used, "%s%02X",
                                   i ? ":" : "", bytes[i]);
    }
}

/* '\n'-separated adapter snapshot, one line per interface:
 *   name '\t' index '\t' up(0|1) '\t' mac '\t' addr[,addr...]
 * getifaddrs reports one node per address, so addresses fold into the line
 * of the interface that owns them (matching GetAdaptersAddresses). */
const char *zan_plat_net_interfaces(void) {
    zan_net_text[0] = '\0';
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) return zan_net_text;

    unsigned used = 0;
    for (struct ifaddrs *it = list; it; it = it->ifa_next) {
        if (!it->ifa_name) continue;
        int seen = 0;
        for (struct ifaddrs *p = list; p != it; p = p->ifa_next) {
            if (p->ifa_name && strcmp(p->ifa_name, it->ifa_name) == 0) {
                seen = 1; break;
            }
        }
        if (seen) continue;

        char mac[32];
        mac[0] = '\0';
        int up = (it->ifa_flags & IFF_UP) && (it->ifa_flags & IFF_RUNNING);
        char addrs[2048];
        unsigned addr_used = 0;
        addrs[0] = '\0';
        for (struct ifaddrs *p = list; p; p = p->ifa_next) {
            if (!p->ifa_name || strcmp(p->ifa_name, it->ifa_name) != 0) continue;
            if (!p->ifa_addr) continue;
            if (!mac[0]) zan_mac_from_sockaddr(p->ifa_addr, mac, sizeof mac);
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
            addr_used += (unsigned)snprintf(addrs + addr_used,
                                            sizeof addrs - addr_used, "%s%s",
                                            addr_used ? "," : "", text);
        }

        unsigned index = if_nametoindex(it->ifa_name);
        int written = snprintf(zan_net_text + used, ZAN_PLAT_TEXT_MAX - used,
                               "%s\t%u\t%d\t%s\t%s\n", it->ifa_name, index,
                               up ? 1 : 0, mac, addrs);
        if (written < 0 || (unsigned)written >= ZAN_PLAT_TEXT_MAX - used) break;
        used += (unsigned)written;
    }
    freeifaddrs(list);
    return zan_net_text;
}

static unsigned short zan_icmp_checksum(const void *data, unsigned len) {
    const unsigned char *bytes = (const unsigned char *)data;
    unsigned int sum = 0;
    while (len > 1) {
        sum += (unsigned int)((bytes[0] << 8) | bytes[1]);
        bytes += 2;
        len -= 2;
    }
    if (len) sum += (unsigned int)(bytes[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (unsigned short)~sum;
}

static long long zan_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

/* One ICMP echo request to the IPv4 literal `address`, wait up to
 * timeout_ms. Returns the RTT in ms (>= 0), or -1 timeout, -2 unreachable,
 * -3 socket/permission error, -4 malformed address. SOCK_RAW needs root or
 * CAP_NET_RAW on macOS; the SOCK_DGRAM ping-socket form is tried first. */
int zan_plat_icmp_ping(const char *address, int timeout_ms) {
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
    unsigned short ident = (unsigned short)(getpid() & 0xFFFF);
    packet[4] = (unsigned char)(ident >> 8);
    packet[5] = (unsigned char)(ident & 0xFF);
    packet[6] = 0;
    packet[7] = 1;                                  /* sequence */
    memcpy(packet + 8, "zan-ping", 8);
    unsigned short sum = zan_icmp_checksum(packet, sizeof packet);
    packet[2] = (unsigned char)(sum >> 8);
    packet[3] = (unsigned char)(sum & 0xFF);

    long long start = zan_now_us();
    if (sendto(fd, packet, sizeof packet, 0, (struct sockaddr *)&dst,
               sizeof dst) < 0) {
        int err = errno;
        close(fd);
        if (err == EHOSTUNREACH || err == ENETUNREACH) return -2;
        return -3;
    }

    for (;;) {
        long long elapsed_ms = (zan_now_us() - start) / 1000;
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
        long got = recvfrom(fd, reply, sizeof reply, 0,
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
            long long rtt_us = zan_now_us() - start;
            close(fd);
            long long rtt_ms = rtt_us / 1000;
            return (int)(rtt_ms > 0x7FFFFFFF ? 0x7FFFFFFF : rtt_ms);
        }
        if (type == 3 || type == 11) {              /* unreachable / TTL */
            close(fd);
            return -2;
        }
        /* anything else is not an answer: keep waiting */
    }
}
#endif /* ZAN_RT_CORE_ZAN */
