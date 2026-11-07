/* rt_io_addr.c: 套接字地址提取、IP 规范化与安全证书策略实现 */

const char *zan_io_socket_peer_ip(intptr_t fd) {
    struct sockaddr_storage peer;
#if defined(_WIN32)
    int length = (int)sizeof(peer);
    if (getpeername((SOCKET)fd, (struct sockaddr *)&peer, &length) != 0) return "";
#else
    socklen_t length = (socklen_t)sizeof(peer);
    if (getpeername((int)fd, (struct sockaddr *)&peer, &length) != 0) return "";
#endif
    return zan_io_sockaddr_ip_str(&peer);
}

/* 格式化网络套接字地址为点分十进制/冒号文本 */
const char *zan_io_sockaddr_ip_str(const void *sa) {
    /* 线程局部缓冲区，支持多工作线程并发无锁格式化 */
    static _Thread_local char address[INET6_ADDRSTRLEN];
    const struct sockaddr *a = (const struct sockaddr *)sa;
    if (a->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)a;
        if (!inet_ntop(AF_INET, &in->sin_addr, address, sizeof(address)))
            return "";
        return address;
    }
    if (a->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)a;
        if (!inet_ntop(AF_INET6, &in6->sin6_addr, address, sizeof(address)))
            return "";
        return address;
    }
    return "";
}

static int32_t zan_io_ip_copy_out(const char *text, char *buf, int32_t cap) {
    if (!buf || cap <= 0) return -1;
    size_t n = strlen(text);
    if ((size_t)cap <= n) return -1;
    memcpy(buf, text, n + 1);
    return (int32_t)n;
}

int32_t zan_io_socket_peer_ip_into(intptr_t fd, char *buf, int32_t cap) {
    struct sockaddr_storage peer;
#if defined(_WIN32)
    int length = (int)sizeof(peer);
    if (getpeername((SOCKET)fd, (struct sockaddr *)&peer, &length) != 0)
        return -1;
#else
    socklen_t length = (socklen_t)sizeof(peer);
    if (getpeername((int)fd, (struct sockaddr *)&peer, &length) != 0)
        return -1;
#endif
    return zan_io_ip_copy_out(zan_io_sockaddr_ip_str(&peer), buf, cap);
}

int32_t zan_io_sockaddr_ip_str_into(const void *sa, char *buf, int32_t cap) {
    return zan_io_ip_copy_out(zan_io_sockaddr_ip_str(sa), buf, cap);
}

/* 解析主机名或字面量 IP 为完整 sockaddr 结构 */
int32_t zan_io_resolve_sa(const char *name, int32_t port, void *buf,
                          int32_t cap) {
    if (!name || !*name) return 0;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = strchr(name, ':') ? AF_UNSPEC : AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res) {
        if (hints.ai_family == AF_INET) {
            hints.ai_family = AF_UNSPEC;
            if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res)
                return 0;
        } else {
            return 0;
        }
    }
    int len = (res->ai_family == AF_INET6) ? (int)sizeof(struct sockaddr_in6)
             : (res->ai_family == AF_INET) ? (int)sizeof(struct sockaddr_in)
             : 0;
    int32_t r = 0;
    if (len && len <= cap && res->ai_addrlen == (size_t)len) {
        memcpy(buf, res->ai_addr, (size_t)len);
        r = len;
    }
    freeaddrinfo(res);
    return r;
}

/* 域名解析 IPv4 网络序地址 */
int32_t zan_io_resolve_ipv4(const char *hostname) {
    if (!hostname || !*hostname) return 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(hostname, NULL, &hints, &res) != 0 || !res) return 0;
    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    uint32_t addr_bits = 0;
    if (res->ai_family == AF_INET && sin->sin_family == AF_INET)
        addr_bits = sin->sin_addr.s_addr;
    freeaddrinfo(res);
    int32_t addr;
    memcpy(&addr, &addr_bits, sizeof(addr));
    return addr;
}

int32_t zan_io_sockaddr_family(const void *sa, int32_t len) {
    if (!sa || len < 2) return 0;
    const struct sockaddr *a = (const struct sockaddr *)sa;
    if (a->sa_family == AF_INET && len >= (int32_t)sizeof(struct sockaddr_in))
        return AF_INET;
    if (a->sa_family == AF_INET6 && len >= (int32_t)sizeof(struct sockaddr_in6))
        return AF_INET6;
    return 0;
}

/* 连接目标地址类别判定 (环回/私网/公网) */
int32_t zan_io_sockaddr_is_safe_ex(const void *sa, int32_t len,
                                  int32_t allow_loopback, int32_t allow_private) {
    int family = zan_io_sockaddr_family(sa, len);
    if (family == AF_INET) {
        const struct sockaddr_in *v4 = (const struct sockaddr_in *)sa;
        uint32_t a = ntohl(v4->sin_addr.s_addr);
        uint32_t first = a >> 24;
        uint32_t second = (a >> 16) & 255u;
        if (first == 127u) return allow_loopback ? 1 : 0;
        if (first == 0u || first >= 224u || first >= 240u)
            return 0;
        /* RFC 1918 私网与链路本地/CGNAT 地址判定 */
        if (first == 10u)
            return allow_private ? 1 : 0;
        if (first == 169u && second == 254u)
            return allow_private ? 1 : 0;
        if (first == 172u && second >= 16u && second <= 31u)
            return allow_private ? 1 : 0;
        if (first == 192u && second == 168u)
            return allow_private ? 1 : 0;
        if (first == 100u && second >= 64u && second <= 127u)
            return allow_private ? 1 : 0;

        if (first == 192u && second == 0u) return 0;
        uint32_t third = (a >> 8) & 255u;
        if (first == 192u && second == 2u) return 0;
        if (first == 192u && second == 88u && third == 99u) return 0;
        if (first == 198u && second >= 18u && second <= 19u) return 0;
        if (first == 198u && second == 51u && third == 100u) return 0;
        if (first == 203u && second == 0u && third == 113u) return 0;
        if (a == 0xffffffffu) return 0;
        return 1;
    }
    if (family == AF_INET6) {
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)sa;
        const unsigned char *b = (const unsigned char *)&v6->sin6_addr;
        int all_zero = 1;
        for (int i = 0; i < 16; i++) if (b[i] != 0) all_zero = 0;
        if (all_zero) return 0;
        int loop = 1;
        for (int i = 0; i < 15; i++) if (b[i] != 0) loop = 0;
        if (loop && b[15] == 1) return allow_loopback ? 1 : 0;
        if ((b[0] & 0xfeu) == 0xfcu) return allow_private ? 1 : 0;       /* ULA fc00::/7 */
        if ((b[0] & 0xfeu) == 0xfeu && (b[1] & 0xc0u) == 0x80u)
            return allow_private ? 1 : 0;                                 /* link-local fe80::/10 */
        if (b[0] == 0xffu) return 0;                  /* multicast */
        if (b[0] == 0x20u && b[1] == 0x01u && b[2] == 0x0du
            && b[3] == 0xb8u) return 0;               /* documentation */
        int mapped = 1;
        for (int i = 0; i < 10; i++) if (b[i] != 0) mapped = 0;
        if (mapped && b[10] == 0xffu && b[11] == 0xffu) {
            struct sockaddr_in mapped4;
            memset(&mapped4, 0, sizeof(mapped4));
            mapped4.sin_family = AF_INET;
            memcpy(&mapped4.sin_addr, b + 12, 4);
            return zan_io_sockaddr_is_safe_ex(&mapped4,
                (int32_t)sizeof(mapped4), allow_loopback, allow_private);
        }
        /* Teredo 隧道 IPv6 地址解析 */
        if (b[0] == 0x20u && b[1] == 0x01u && b[2] == 0x00u && b[3] == 0x00u) {
            struct sockaddr_in t4;
            unsigned char cli[4];
            memset(&t4, 0, sizeof(t4));
            t4.sin_family = AF_INET;
            memcpy(&t4.sin_addr, b + 4, 4);
            if (!zan_io_sockaddr_is_safe_ex(&t4, (int32_t)sizeof(t4), allow_loopback, allow_private))
                return 0;
            for (int i = 0; i < 4; i++) cli[i] = (unsigned char)(b[12 + i] ^ 0xffu);
            memcpy(&t4.sin_addr, cli, 4);
            return zan_io_sockaddr_is_safe_ex(&t4, (int32_t)sizeof(t4), allow_loopback, allow_private);
        }
        int v4_off = -1;
        if (b[0] == 0x20u && b[1] == 0x02u)
            v4_off = 2;
        else if (b[0] == 0x00u && b[1] == 0x64u && b[2] == 0xffu && b[3] == 0x9bu)
            v4_off = 12;
        else if (mapped)
            v4_off = 12;
        if (v4_off >= 0) {
            struct sockaddr_in e4;
            memset(&e4, 0, sizeof(e4));
            e4.sin_family = AF_INET;
            memcpy(&e4.sin_addr, b + v4_off, 4);
            return zan_io_sockaddr_is_safe_ex(&e4, (int32_t)sizeof(e4), allow_loopback, allow_private);
        }
        return 1;
    }
    return 0;
}

int32_t zan_io_sockaddr_is_safe(const void *sa, int32_t len,
                               int32_t allow_loopback) {
    return zan_io_sockaddr_is_safe_ex(sa, len, allow_loopback, 0);
}

int32_t zan_io_resolve_all(const char *name, int32_t port, void *buf,
                           int32_t cap) {
    if (!buf || cap <= 0) return 0;
    if (!name || !*name || cap < ZAN_IO_SA_STRIDE) {
        memset(buf, 0, (size_t)cap);
        return 0;
    }

    /* 快路径 1: 尝试直接按 IPv4 字面量解析，避开 getaddrinfo 调度开销与网络风暴 */
    struct sockaddr_in sin4;
    memset(&sin4, 0, sizeof(sin4));
    sin4.sin_family = AF_INET;
    sin4.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, name, &sin4.sin_addr) == 1) {
        memset(buf, 0, (size_t)cap);
        memcpy(buf, &sin4, sizeof(sin4));
        return 1;
    }

    /* 快路径 2: 尝试直接按 IPv6 字面量解析（支持带方括号形态 [::1]） */
    const char *v6name = name;
    char v6strip[64];
    if (name[0] == '[') {
        size_t l = strlen(name);
        if (l > 2 && l < sizeof(v6strip) && name[l - 1] == ']') {
            memcpy(v6strip, name + 1, l - 2);
            v6strip[l - 2] = '\0';
            v6name = v6strip;
        }
    }
    struct sockaddr_in6 sin6;
    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons((uint16_t)port);
    if (inet_pton(AF_INET6, v6name, &sin6.sin6_addr) == 1) {
        memset(buf, 0, (size_t)cap);
        memcpy(buf, &sin6, sizeof(sin6));
        return 1;
    }

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    struct addrinfo *res = NULL;
    if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res) {
        memset(buf, 0, (size_t)cap);
        return 0;
    }
    int32_t capacity = cap / ZAN_IO_SA_STRIDE;
    int32_t count = 0;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        int32_t family = zan_io_sockaddr_family(p->ai_addr,
            (int32_t)p->ai_addrlen);
        int32_t len = family == AF_INET6 ? (int32_t)sizeof(struct sockaddr_in6)
                     : family == AF_INET ? (int32_t)sizeof(struct sockaddr_in) : 0;
        if (!len || p->ai_addrlen < (size_t)len) continue;
        int duplicate = 0;
        for (int32_t i = 0; i < count; i++) {
            unsigned char *old = (unsigned char *)buf + i * ZAN_IO_SA_STRIDE;
            int oldfam = zan_io_sockaddr_family(old, len);
            if (oldfam == family && memcmp(old, p->ai_addr, (size_t)len) == 0) {
                duplicate = 1; break;
            }
        }
        if (duplicate) continue;
        if (count >= capacity) {
            memset(buf, 0, (size_t)cap);
            freeaddrinfo(res);
            return 0;
        }
        unsigned char *dst = (unsigned char *)buf + count * ZAN_IO_SA_STRIDE;
        memset(dst, 0, ZAN_IO_SA_STRIDE);
        memcpy(dst, p->ai_addr, (size_t)len);
        count++;
    }
    freeaddrinfo(res);
    return count;
}

int64_t zan_io_resolve_all_async(intptr_t name_ptr, int32_t port,
                                 intptr_t buf_ptr, int32_t cap) {
    return (int64_t)zan_io_resolve_all((const char *)name_ptr, port,
                                       (void *)buf_ptr, cap);
}

#if defined(_WIN32)
/* 从证书上下文解析 DER 证书数据指针与长度 */
const unsigned char *zan_crypto_cert_encoded(const void *cert, int *out_len) {
    const CERT_CONTEXT *ctx = (const CERT_CONTEXT *)cert;
    if (!ctx || !out_len) return NULL;
    *out_len = (int)ctx->cbCertEncoded;
    return ctx->pbCertEncoded;
}

int32_t zan_io_crypto_windows_ssl_policy(const unsigned char *certs, int32_t total_len,
                                       int32_t count, const char *host, int32_t host_len) {
    typedef PCCERT_CONTEXT (WINAPI *create_cert_fn)(DWORD, const BYTE *, DWORD);
    typedef HCERTSTORE (WINAPI *open_store_fn)(LPCSTR, DWORD, HCRYPTPROV_LEGACY, DWORD, const void *);
    typedef BOOL (WINAPI *add_cert_fn)(HCERTSTORE, DWORD, const BYTE *, DWORD, DWORD, PCCERT_CONTEXT *);
    typedef BOOL (WINAPI *get_chain_fn)(HCERTCHAINENGINE, PCCERT_CONTEXT, LPFILETIME,
                                        HCERTSTORE, PCERT_CHAIN_PARA, DWORD, LPVOID, PCCERT_CHAIN_CONTEXT *);
    typedef BOOL (WINAPI *verify_policy_fn)(LPCSTR, PCCERT_CHAIN_CONTEXT,
                                            PCERT_CHAIN_POLICY_PARA, PCERT_CHAIN_POLICY_STATUS);
    typedef BOOL (WINAPI *close_store_fn)(HCERTSTORE, DWORD);
    typedef BOOL (WINAPI *free_cert_fn)(PCCERT_CONTEXT);
    typedef void (WINAPI *free_chain_fn)(PCCERT_CHAIN_CONTEXT);
    HMODULE lib = NULL;
    HCERTSTORE store = NULL;
    PCCERT_CONTEXT leaf = NULL;
    PCCERT_CHAIN_CONTEXT chain = NULL;
    int32_t result = 0;
    WCHAR wide_host[254];
    create_cert_fn create_cert;
    open_store_fn open_store;
    add_cert_fn add_cert;
    get_chain_fn get_chain;
    verify_policy_fn verify_policy;
    close_store_fn close_store;
    free_cert_fn free_cert;
    free_chain_fn free_chain;

    if (!certs || !host || total_len < 5 || total_len > 524288 ||
        count < 1 || count > 8 || host_len < 1 || host_len > 253) return 0;
    for (int32_t i = 0; i < host_len; ++i) {
        unsigned char ch = (unsigned char)host[i];
        if (ch <= 32 || ch >= 127 || ch == '/' || ch == '\\' || ch == ':') return 0;
        wide_host[i] = (WCHAR)ch;
    }
    wide_host[host_len] = 0;

    lib = LoadLibraryW(L"crypt32.dll");
    if (!lib) return -1;
    create_cert = (create_cert_fn)GetProcAddress(lib, "CertCreateCertificateContext");
    open_store = (open_store_fn)GetProcAddress(lib, "CertOpenStore");
    add_cert = (add_cert_fn)GetProcAddress(lib, "CertAddEncodedCertificateToStore");
    get_chain = (get_chain_fn)GetProcAddress(lib, "CertGetCertificateChain");
    verify_policy = (verify_policy_fn)GetProcAddress(lib, "CertVerifyCertificateChainPolicy");
    close_store = (close_store_fn)GetProcAddress(lib, "CertCloseStore");
    free_cert = (free_cert_fn)GetProcAddress(lib, "CertFreeCertificateContext");
    free_chain = (free_chain_fn)GetProcAddress(lib, "CertFreeCertificateChain");
    if (!create_cert || !open_store || !add_cert || !get_chain || !verify_policy ||
        !close_store || !free_cert || !free_chain) { FreeLibrary(lib); return -1; }

    store = open_store(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
    if (!store) goto cleanup;
    int32_t pos = 0;
    for (int32_t i = 0; i < count; ++i) {
        if (total_len - pos < 4) goto cleanup;
        uint32_t n = (uint32_t)certs[pos] | ((uint32_t)certs[pos+1] << 8) |
                     ((uint32_t)certs[pos+2] << 16) | ((uint32_t)certs[pos+3] << 24);
        pos += 4;
        if (n < 1 || n > 65536 || n > (uint32_t)(total_len - pos)) goto cleanup;
        if (i == 0) {
            leaf = create_cert(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, certs + pos, n);
            if (!leaf) goto cleanup;
        } else if (!add_cert(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                             certs + pos, n, CERT_STORE_ADD_ALWAYS, NULL)) {
            goto cleanup;
        }
        pos += (int32_t)n;
    }
    if (pos != total_len) goto cleanup;

    CERT_CHAIN_PARA chain_para;
    memset(&chain_para, 0, sizeof(chain_para));
    chain_para.cbSize = sizeof(chain_para);
    LPSTR server_auth_oid = szOID_PKIX_KP_SERVER_AUTH;
    chain_para.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    chain_para.RequestedUsage.Usage.cUsageIdentifier = 1;
    chain_para.RequestedUsage.Usage.rgpszUsageIdentifier = &server_auth_oid;
    if (!get_chain(NULL, leaf, NULL, store, &chain_para,
                   CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT |
                   CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL | CERT_CHAIN_DISABLE_AIA,
                   NULL, &chain) || !chain) { result = -2; goto cleanup; }
    {
        uint32_t trust_err = chain->TrustStatus.dwErrorStatus;
        if (trust_err != 0) {
            if (trust_err & CERT_TRUST_IS_PARTIAL_CHAIN) result = -3;
            else if (trust_err & CERT_TRUST_IS_UNTRUSTED_ROOT) result = -4;
            else if (trust_err & CERT_TRUST_IS_REVOKED) result = -10;
            else if (trust_err & CERT_TRUST_IS_NOT_TIME_VALID) result = -5;
            else if (trust_err & CERT_TRUST_REVOCATION_STATUS_UNKNOWN) result = -6;
            else result = -7;
            goto cleanup;
        }
    }

    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl;
    memset(&ssl, 0, sizeof(ssl));
    ssl.cbSize = sizeof(ssl);
    ssl.dwAuthType = AUTHTYPE_SERVER;
    ssl.pwszServerName = wide_host;
    CERT_CHAIN_POLICY_PARA policy;
    memset(&policy, 0, sizeof(policy));
    policy.cbSize = sizeof(policy);
    policy.pvExtraPolicyPara = &ssl;
    CERT_CHAIN_POLICY_STATUS status;
    memset(&status, 0, sizeof(status));
    status.cbSize = sizeof(status);
    if (!verify_policy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status)) { result = -9; goto cleanup; }
    if (status.dwError != 0) {
        result = ((HRESULT)status.dwError == CERT_E_CN_NO_MATCH) ? -8 : -9;
        goto cleanup;
    }
    result = 1;
cleanup:
    if (chain) free_chain(chain);
    if (leaf) free_cert(leaf);
    if (store) close_store(store, 0);
    FreeLibrary(lib);
    return result;
}
#endif

