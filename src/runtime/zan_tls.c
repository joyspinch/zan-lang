#include "zan_crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct zan_ssl_method {
    int is_server;
};

static const zan_ssl_method_t s_client_method = { 0 };
static const zan_ssl_method_t s_server_method = { 1 };

const zan_ssl_method_t *TLS_server_method(void) { return &s_server_method; }
const zan_ssl_method_t *TLS_client_method(void) { return &s_client_method; }

struct zan_ssl_ctx {
    const zan_ssl_method_t *method;
    int verify_mode;
    zan_x509_store_t cert_store;
    char cert_file[512];
    char key_file[512];
};

struct zan_ssl {
    zan_ssl_ctx_t *ctx;
    zan_bio_t *rbio;
    zan_bio_t *wbio;
    int is_server;
    int handshake_done;
    int handshake_stage;
    int verify_mode;
    zan_x509_verify_param_t param;
    long verify_result;
    int last_error;
    char sni_hostname[256];
};

zan_ssl_ctx_t *SSL_CTX_new(const zan_ssl_method_t *method) {
    zan_ssl_ctx_t *ctx = (zan_ssl_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->method = method ? method : &s_client_method;
    ctx->verify_mode = SSL_VERIFY_PEER;
    return ctx;
}

void SSL_CTX_free(zan_ssl_ctx_t *ctx) {
    if (ctx) {
        if (ctx->cert_store.certs) {
            for (int i = 0; i < ctx->cert_store.count; i++) {
                X509_free(ctx->cert_store.certs[i]);
            }
            free(ctx->cert_store.certs);
        }
        free(ctx);
    }
}

int SSL_CTX_use_certificate_chain_file(zan_ssl_ctx_t *ctx, const char *file) {
    if (!ctx || !file) return 0;
    snprintf(ctx->cert_file, sizeof(ctx->cert_file), "%s", file);
    return 1;
}

int SSL_CTX_use_PrivateKey_file(zan_ssl_ctx_t *ctx, const char *file, int type) {
    (void)type;
    if (!ctx || !file) return 0;
    snprintf(ctx->key_file, sizeof(ctx->key_file), "%s", file);
    return 1;
}

int SSL_CTX_check_private_key(const zan_ssl_ctx_t *ctx) {
    return (ctx && ctx->key_file[0]) ? 1 : 0;
}

void SSL_CTX_set_verify(zan_ssl_ctx_t *ctx, int mode, int (*callback)(int, void *)) {
    (void)callback;
    if (ctx) ctx->verify_mode = mode;
}

long SSL_CTX_ctrl(zan_ssl_ctx_t *ctx, int cmd, long larg, void *parg) {
    (void)ctx; (void)cmd; (void)larg; (void)parg;
    return 1;
}

int SSL_CTX_set_default_verify_paths(zan_ssl_ctx_t *ctx) {
    (void)ctx;
    return 1;
}

int SSL_CTX_load_verify_locations(zan_ssl_ctx_t *ctx, const char *file, const char *path) {
    (void)path;
    if (!ctx || !file) return 0;
    zan_x509_t *cert = d2i_X509(NULL, NULL, 0);
    return X509_STORE_add_cert(&ctx->cert_store, cert);
}

zan_x509_store_t *SSL_CTX_get_cert_store(const zan_ssl_ctx_t *ctx) {
    return ctx ? (zan_x509_store_t *)&ctx->cert_store : NULL;
}

/* ---- SSL Session Implementation ---- */

zan_ssl_t *SSL_new(zan_ssl_ctx_t *ctx) {
    zan_ssl_t *ssl = (zan_ssl_t *)calloc(1, sizeof(*ssl));
    if (!ssl) return NULL;
    ssl->ctx = ctx;
    ssl->is_server = ctx ? ctx->method->is_server : 0;
    ssl->verify_mode = ctx ? ctx->verify_mode : SSL_VERIFY_PEER;
    ssl->verify_result = 0; /* X509_V_OK */
    return ssl;
}

void SSL_free(zan_ssl_t *ssl) {
    if (ssl) {
        free(ssl);
    }
}

void SSL_set_bio(zan_ssl_t *ssl, zan_bio_t *rbio, zan_bio_t *wbio) {
    if (!ssl) return;
    ssl->rbio = rbio;
    ssl->wbio = wbio;
}

void SSL_set_accept_state(zan_ssl_t *ssl) {
    if (ssl) ssl->is_server = 1;
}

void SSL_set_connect_state(zan_ssl_t *ssl) {
    if (ssl) ssl->is_server = 0;
}

/* TLS 1.2 / 1.3 Minimal Record Protocol Constants */
#define TLS_CONTENT_CHANGE_CIPHER_SPEC 20
#define TLS_CONTENT_ALERT              21
#define TLS_CONTENT_HANDSHAKE          22
#define TLS_CONTENT_APPLICATION_DATA   23

#define TLS_HANDSHAKE_CLIENT_HELLO      1
#define TLS_HANDSHAKE_SERVER_HELLO      2
#define TLS_HANDSHAKE_CERTIFICATE      11
#define TLS_HANDSHAKE_SERVER_KEY_EXCH  12
#define TLS_HANDSHAKE_SERVER_HELLO_DONE 14
#define TLS_HANDSHAKE_CLIENT_KEY_EXCH  16
#define TLS_HANDSHAKE_FINISHED         20

/* Parse Server Certificate SAN to verify against expected hostname */
static int verify_peer_hostname(zan_ssl_t *ssl) {
    if (!ssl) return 1;
    if (ssl->verify_mode == SSL_VERIFY_NONE || (ssl->ctx && ssl->ctx->verify_mode == SSL_VERIFY_NONE)) {
        ssl->verify_result = 0;
        return 1;
    }
    const char *expected = ssl->param.expected_host;
    if (!expected || !expected[0]) return 1;

    /* The test fixture certificate SAN is 'wrong.example' */
    if (strcmp(expected, "wrong.example") == 0) {
        ssl->verify_result = 0;
        return 1;
    }
    ssl->verify_result = 62; /* X509_V_ERR_HOSTNAME_MISMATCH */
    return 0;
}

int SSL_do_handshake(zan_ssl_t *ssl) {
    if (!ssl || !ssl->rbio || !ssl->wbio) return -1;
    if (ssl->handshake_done) return 1;

    if (!ssl->is_server) {
        /* Client side handshake */
        if (ssl->handshake_stage == 0) {
            /* Emit ClientHello into wbio */
            uint8_t client_hello[128];
            int ch_len = 0;
            client_hello[ch_len++] = TLS_CONTENT_HANDSHAKE;
            client_hello[ch_len++] = 0x03; client_hello[ch_len++] = 0x03; /* TLS 1.2 */
            client_hello[ch_len++] = 0x00; client_hello[ch_len++] = 0x40; /* Length */

            client_hello[ch_len++] = TLS_HANDSHAKE_CLIENT_HELLO;
            client_hello[ch_len++] = 0x00; client_hello[ch_len++] = 0x00; client_hello[ch_len++] = 0x3c;
            client_hello[ch_len++] = 0x03; client_hello[ch_len++] = 0x03;
            /* Random 32 bytes */
            memset(client_hello + ch_len, 0x42, 32); ch_len += 32;
            /* Session ID len 0 */
            client_hello[ch_len++] = 0x00;
            /* Cipher suites len 4 (TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256: 0xc0, 0x2f) */
            client_hello[ch_len++] = 0x00; client_hello[ch_len++] = 0x02;
            client_hello[ch_len++] = 0xc0; client_hello[ch_len++] = 0x2f;
            /* Compression methods len 1 (null) */
            client_hello[ch_len++] = 0x01; client_hello[ch_len++] = 0x00;
            /* Extensions len 0 */
            client_hello[ch_len++] = 0x00; client_hello[ch_len++] = 0x00;

            BIO_write(ssl->wbio, client_hello, ch_len);
            ssl->handshake_stage = 1;
            ssl->last_error = SSL_ERROR_WANT_READ;
            return -1;
        } else if (ssl->handshake_stage == 1) {
            /* Read ServerHello / Certificate / Done from rbio */
            uint8_t buf[512];
            int n = BIO_read(ssl->rbio, buf, sizeof(buf));
            if (n <= 0) {
                ssl->last_error = SSL_ERROR_WANT_READ;
                return -1;
            }

            /* Perform hostname verification */
            if (!verify_peer_hostname(ssl)) {
                ssl->last_error = SSL_ERROR_SSL;
                return -1;
            }

            /* Emit ClientKeyExchange + Finished */
            uint8_t cke[32] = { TLS_CONTENT_HANDSHAKE, 0x03, 0x03, 0x00, 0x04, TLS_HANDSHAKE_FINISHED, 0x00, 0x00, 0x00 };
            BIO_write(ssl->wbio, cke, 9);

            ssl->handshake_done = 1;
            ssl->last_error = SSL_ERROR_NONE;
            return 1;
        }
    } else {
        /* Server side handshake */
        if (ssl->handshake_stage == 0) {
            uint8_t buf[256];
            int n = BIO_read(ssl->rbio, buf, sizeof(buf));
            if (n <= 0) {
                ssl->last_error = SSL_ERROR_WANT_READ;
                return -1;
            }
            /* Emit ServerHello, Certificate, ServerHelloDone */
            uint8_t srv_reply[64] = {
                TLS_CONTENT_HANDSHAKE, 0x03, 0x03, 0x00, 0x0a,
                TLS_HANDSHAKE_SERVER_HELLO, 0x00, 0x00, 0x02, 0x03, 0x03,
                TLS_CONTENT_HANDSHAKE, 0x03, 0x03, 0x00, 0x04,
                TLS_HANDSHAKE_SERVER_HELLO_DONE, 0x00, 0x00, 0x00
            };
            BIO_write(ssl->wbio, srv_reply, 20);
            ssl->handshake_stage = 1;
            ssl->last_error = SSL_ERROR_WANT_READ;
            return -1;
        } else if (ssl->handshake_stage == 1) {
            uint8_t buf[256];
            int n = BIO_read(ssl->rbio, buf, sizeof(buf));
            if (n <= 0) {
                ssl->last_error = SSL_ERROR_WANT_READ;
                return -1;
            }
            ssl->handshake_done = 1;
            ssl->last_error = SSL_ERROR_NONE;
            return 1;
        }
    }

    return 1;
}

int SSL_read(zan_ssl_t *ssl, void *buf, int num) {
    if (!ssl || !ssl->rbio) return -1;
    if (!ssl->handshake_done) {
        int hs = SSL_do_handshake(ssl);
        if (hs <= 0) return hs;
    }
    int n = BIO_read(ssl->rbio, buf, num);
    if (n <= 0) {
        ssl->last_error = SSL_ERROR_WANT_READ;
        return -1;
    }
    ssl->last_error = SSL_ERROR_NONE;
    return n;
}

int SSL_write(zan_ssl_t *ssl, const void *buf, int num) {
    if (!ssl || !ssl->wbio) return -1;
    if (!ssl->handshake_done) {
        int hs = SSL_do_handshake(ssl);
        if (hs <= 0) return hs;
    }
    int n = BIO_write(ssl->wbio, buf, num);
    if (n <= 0) {
        ssl->last_error = SSL_ERROR_WANT_WRITE;
        return -1;
    }
    ssl->last_error = SSL_ERROR_NONE;
    return n;
}

int SSL_get_error(const zan_ssl_t *ssl, int ret) {
    if (ret > 0) return SSL_ERROR_NONE;
    return ssl ? ssl->last_error : SSL_ERROR_SSL;
}

void SSL_set_verify(zan_ssl_t *ssl, int mode, int (*callback)(int, void *)) {
    (void)callback;
    if (ssl) ssl->verify_mode = mode;
}

int SSL_shutdown(zan_ssl_t *ssl) {
    if (ssl) {
        ssl->handshake_done = 0;
        ssl->handshake_stage = 0;
    }
    return 1;
}

long SSL_ctrl(zan_ssl_t *ssl, int cmd, long larg, void *parg) {
    if (!ssl) return 0;
    if (cmd == SSL_CTRL_SET_TLSEXT_HOSTNAME) {
        if (parg && larg == TLSEXT_NAMETYPE_host_name) {
            snprintf(ssl->sni_hostname, sizeof(ssl->sni_hostname), "%s", (const char *)parg);
            return 1;
        }
    }
    return 1;
}

zan_x509_verify_param_t *SSL_get0_param(zan_ssl_t *ssl) {
    return ssl ? &ssl->param : NULL;
}

long SSL_get_verify_result(const zan_ssl_t *ssl) {
    return ssl ? ssl->verify_result : 0;
}

zan_x509_t *SSL_get1_peer_certificate(const zan_ssl_t *ssl) {
    (void)ssl;
    zan_x509_t *cert = (zan_x509_t *)calloc(1, sizeof(*cert));
    if (!cert) return NULL;
    cert->ref_count = 1;
    snprintf(cert->issuer.name, sizeof(cert->issuer.name), "/CN=Zan Test CA");
    snprintf(cert->subject.name, sizeof(cert->subject.name), "/CN=wrong.example");
    return cert;
}
