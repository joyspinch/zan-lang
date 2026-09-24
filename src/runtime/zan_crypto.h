#ifndef ZAN_CRYPTO_H
#define ZAN_CRYPTO_H

#include "zan_c_portable.h"

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(ZAN_CRYPTO_BUILD_DLL)
    #define ZAN_CRYPTO_API __declspec(dllexport)
  #else
    #define ZAN_CRYPTO_API
  #endif
#else
  #if defined(__GNUC__) && __GNUC__ >= 4
    #define ZAN_CRYPTO_API __attribute__((visibility("default")))
  #else
    #define ZAN_CRYPTO_API
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Forward declarations ---- */
typedef struct zan_evp_cipher zan_evp_cipher_t;
typedef struct zan_evp_cipher_ctx zan_evp_cipher_ctx_t;
typedef struct zan_bio_method { int type; const char *name; } zan_bio_method_t;
typedef struct zan_bio zan_bio_t;

typedef struct zan_x509_name {
    char name[256];
} zan_x509_name_t;

typedef struct zan_x509 {
    int ref_count;
    zan_x509_name_t issuer;
    zan_x509_name_t subject;
    char san_dns[8][128];
    int san_count;
} zan_x509_t;

typedef struct zan_x509_store {
    zan_x509_t **certs;
    int count;
    int cap;
} zan_x509_store_t;

typedef struct zan_x509_verify_param {
    char expected_host[256];
    char expected_ip[64];
    unsigned long flags;
} zan_x509_verify_param_t;

typedef struct zan_ssl_method zan_ssl_method_t;
typedef struct zan_ssl_ctx zan_ssl_ctx_t;
typedef struct zan_ssl zan_ssl_t;

/* EVP Cipher types */
enum {
    ZAN_CIPHER_AES_128_CBC = 1,
    ZAN_CIPHER_AES_192_CBC,
    ZAN_CIPHER_AES_256_CBC,
    ZAN_CIPHER_AES_128_ECB,
    ZAN_CIPHER_AES_192_ECB,
    ZAN_CIPHER_AES_256_ECB,
    ZAN_CIPHER_AES_128_CTR,
    ZAN_CIPHER_AES_192_CTR,
    ZAN_CIPHER_AES_256_CTR,
    ZAN_CIPHER_AES_128_GCM,
    ZAN_CIPHER_AES_192_GCM,
    ZAN_CIPHER_AES_256_GCM,
    ZAN_CIPHER_SM4_CBC,
    ZAN_CIPHER_SM4_ECB,
    ZAN_CIPHER_SM4_CTR
};

/* GCM Control commands */
#define EVP_CTRL_GCM_SET_IVLEN       0x09
#define EVP_CTRL_GCM_GET_TAG         0x10
#define EVP_CTRL_GCM_SET_TAG         0x11

/* SSL Constants */
#define SSL_ERROR_NONE               0
#define SSL_ERROR_SSL                1
#define SSL_ERROR_WANT_READ          2
#define SSL_ERROR_WANT_WRITE         3
#define SSL_ERROR_WANT_X509_LOOKUP   4
#define SSL_ERROR_SYSCALL            5
#define SSL_ERROR_ZERO_RETURN        6

#define SSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define TLSEXT_NAMETYPE_host_name    0

#define SSL_VERIFY_NONE              0x00
#define SSL_VERIFY_PEER              0x01
#define SSL_VERIFY_FAIL_IF_NO_PEER_CERT 0x02

/* ---- Crypto EVP API ---- */
ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_128_cbc(void);
ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_192_cbc(void);
ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_256_cbc(void);

ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_128_ecb(void);
ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_192_ecb(void);
ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_256_ecb(void);

ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_128_ctr(void);
ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_192_ctr(void);
ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_256_ctr(void);

ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_128_gcm(void);
ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_192_gcm(void);
ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_aes_256_gcm(void);

ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_sm4_cbc(void);
ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_sm4_ecb(void);
ZAN_CRYPTO_API const zan_evp_cipher_t *EVP_sm4_ctr(void);

ZAN_CRYPTO_API zan_evp_cipher_ctx_t *EVP_CIPHER_CTX_new(void);
ZAN_CRYPTO_API void EVP_CIPHER_CTX_free(zan_evp_cipher_ctx_t *ctx);
ZAN_CRYPTO_API int EVP_CIPHER_CTX_set_padding(zan_evp_cipher_ctx_t *ctx, int pad);
ZAN_CRYPTO_API int EVP_CIPHER_CTX_ctrl(zan_evp_cipher_ctx_t *ctx, int type, int arg, void *ptr);

ZAN_CRYPTO_API int EVP_EncryptInit_ex(zan_evp_cipher_ctx_t *ctx, const zan_evp_cipher_t *cipher,
                                      void *impl, const unsigned char *key, const unsigned char *iv);
ZAN_CRYPTO_API int EVP_EncryptUpdate(zan_evp_cipher_ctx_t *ctx, unsigned char *out, int *outl,
                                     const unsigned char *in, int inl);
ZAN_CRYPTO_API int EVP_EncryptFinal_ex(zan_evp_cipher_ctx_t *ctx, unsigned char *out, int *outl);

ZAN_CRYPTO_API int EVP_DecryptInit_ex(zan_evp_cipher_ctx_t *ctx, const zan_evp_cipher_t *cipher,
                                      void *impl, const unsigned char *key, const unsigned char *iv);
ZAN_CRYPTO_API int EVP_DecryptUpdate(zan_evp_cipher_ctx_t *ctx, unsigned char *out, int *outl,
                                     const unsigned char *in, int inl);
ZAN_CRYPTO_API int EVP_DecryptFinal_ex(zan_evp_cipher_ctx_t *ctx, unsigned char *out, int *outl);

/* ---- BIO API ---- */
ZAN_CRYPTO_API const zan_bio_method_t *BIO_s_mem(void);
ZAN_CRYPTO_API zan_bio_t *BIO_new(const zan_bio_method_t *method);
ZAN_CRYPTO_API int BIO_write(zan_bio_t *bio, const void *data, int dlen);
ZAN_CRYPTO_API int BIO_read(zan_bio_t *bio, void *buf, int blen);
ZAN_CRYPTO_API int BIO_ctrl_pending(zan_bio_t *bio);
ZAN_CRYPTO_API int BIO_free(zan_bio_t *bio);

/* ---- Error API ---- */
ZAN_CRYPTO_API unsigned long ERR_get_error(void);

/* ---- X509 API ---- */
ZAN_CRYPTO_API zan_x509_t *d2i_X509(zan_x509_t **px, const unsigned char **pp, long len);
ZAN_CRYPTO_API void X509_free(zan_x509_t *x);
ZAN_CRYPTO_API int X509_STORE_add_cert(zan_x509_store_t *store, zan_x509_t *x);
ZAN_CRYPTO_API zan_x509_name_t *X509_get_issuer_name(zan_x509_t *x);
ZAN_CRYPTO_API zan_x509_name_t *X509_get_subject_name(zan_x509_t *x);
ZAN_CRYPTO_API char *X509_NAME_oneline(zan_x509_name_t *name, char *buf, int size);
ZAN_CRYPTO_API void *X509_get_X509_PUBKEY(zan_x509_t *x);
ZAN_CRYPTO_API int i2d_X509_PUBKEY(void *pubkey, unsigned char **pp);
ZAN_CRYPTO_API void CRYPTO_free(void *ptr, const char *file, int line);

ZAN_CRYPTO_API int X509_VERIFY_PARAM_set1_host(zan_x509_verify_param_t *param, const char *name, size_t len);
ZAN_CRYPTO_API int X509_VERIFY_PARAM_set1_ip_asc(zan_x509_verify_param_t *param, const char *ipasc);
ZAN_CRYPTO_API void X509_VERIFY_PARAM_set_hostflags(zan_x509_verify_param_t *param, unsigned long flags);
ZAN_CRYPTO_API const char *X509_VERIFY_PARAM_get0_name(zan_x509_verify_param_t *param);

/* ---- SSL / TLS API ---- */
ZAN_CRYPTO_API const zan_ssl_method_t *TLS_server_method(void);
ZAN_CRYPTO_API const zan_ssl_method_t *TLS_client_method(void);

ZAN_CRYPTO_API zan_ssl_ctx_t *SSL_CTX_new(const zan_ssl_method_t *method);
ZAN_CRYPTO_API void SSL_CTX_free(zan_ssl_ctx_t *ctx);
ZAN_CRYPTO_API int SSL_CTX_use_certificate_chain_file(zan_ssl_ctx_t *ctx, const char *file);
ZAN_CRYPTO_API int SSL_CTX_use_PrivateKey_file(zan_ssl_ctx_t *ctx, const char *file, int type);
ZAN_CRYPTO_API int SSL_CTX_check_private_key(const zan_ssl_ctx_t *ctx);
ZAN_CRYPTO_API void SSL_CTX_set_verify(zan_ssl_ctx_t *ctx, int mode, int (*callback)(int, void *));
ZAN_CRYPTO_API long SSL_CTX_ctrl(zan_ssl_ctx_t *ctx, int cmd, long larg, void *parg);
ZAN_CRYPTO_API int SSL_CTX_set_default_verify_paths(zan_ssl_ctx_t *ctx);
ZAN_CRYPTO_API int SSL_CTX_load_verify_locations(zan_ssl_ctx_t *ctx, const char *file, const char *path);
ZAN_CRYPTO_API zan_x509_store_t *SSL_CTX_get_cert_store(const zan_ssl_ctx_t *ctx);

ZAN_CRYPTO_API zan_ssl_t *SSL_new(zan_ssl_ctx_t *ctx);
ZAN_CRYPTO_API void SSL_free(zan_ssl_t *ssl);
ZAN_CRYPTO_API void SSL_set_bio(zan_ssl_t *ssl, zan_bio_t *rbio, zan_bio_t *wbio);
ZAN_CRYPTO_API void SSL_set_accept_state(zan_ssl_t *ssl);
ZAN_CRYPTO_API void SSL_set_connect_state(zan_ssl_t *ssl);
ZAN_CRYPTO_API int SSL_do_handshake(zan_ssl_t *ssl);
ZAN_CRYPTO_API int SSL_read(zan_ssl_t *ssl, void *buf, int num);
ZAN_CRYPTO_API int SSL_write(zan_ssl_t *ssl, const void *buf, int num);
ZAN_CRYPTO_API int SSL_get_error(const zan_ssl_t *ssl, int ret);
ZAN_CRYPTO_API void SSL_set_verify(zan_ssl_t *ssl, int mode, int (*callback)(int, void *));
ZAN_CRYPTO_API int SSL_shutdown(zan_ssl_t *ssl);
ZAN_CRYPTO_API long SSL_ctrl(zan_ssl_t *ssl, int cmd, long larg, void *parg);
ZAN_CRYPTO_API zan_x509_verify_param_t *SSL_get0_param(zan_ssl_t *ssl);
ZAN_CRYPTO_API long SSL_get_verify_result(const zan_ssl_t *ssl);
ZAN_CRYPTO_API zan_x509_t *SSL_get1_peer_certificate(const zan_ssl_t *ssl);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_CRYPTO_H */
