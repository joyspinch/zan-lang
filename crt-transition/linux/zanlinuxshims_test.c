/* Host-side vector test for the digest shims (compiled and run on macOS
 * against the published vectors; the same source serves the guest).
 * Exit 0 = all vectors pass. */

#include <stdio.h>
#include <string.h>
#include "zanlinuxshims.c"

static int fails = 0;

static void hexdig(const unsigned char *d, int n, char *out) {
    for (int i = 0; i < n; i++) {
        sprintf(out + i * 2, "%02x", d[i]);
    }
    out[n * 2] = 0;
}

static void check(const char *name, const char *got, const char *want) {
    if (strcmp(got, want) != 0) {
        printf("FAIL %s: got %s want %s\n", name, got, want);
        fails++;
    }
}

int main(void) {
    unsigned char d[32];
    char hex[65];
    char longbuf[1000001];

    /* FIPS 180 / RFC 1321 vectors */
    hexdig(CC_MD5("", 0, d), 16, hex);
    check("md5-empty", hex, "d41d8cd98f00b204e9800998ecf8427e");
    hexdig(CC_MD5("abc", 3, d), 16, hex);
    check("md5-abc", hex, "900150983cd24fb0d6963f7d28e17f72");
    hexdig(CC_MD5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 62, d), 16, hex);
    check("md5-alnum", hex, "d174ab98d277d9f5a5611c2c9f419d9f");
    hexdig(CC_SHA1("", 0, d), 20, hex);
    check("sha1-empty", hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    hexdig(CC_SHA1("abc", 3, d), 20, hex);
    check("sha1-abc", hex, "a9993e364706816aba3e25717850c26c9cd0d89d");
    hexdig(CC_SHA1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d), 20, hex);
    check("sha1-nopq", hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    hexdig(CC_SHA256("", 0, d), 32, hex);
    check("sha256-empty", hex,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    hexdig(CC_SHA256("abc", 3, d), 32, hex);
    check("sha256-abc", hex,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    hexdig(CC_SHA256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d), 32, hex);
    check("sha256-nopq", hex,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    /* million 'a' vectors (also exercises block-boundary feeding) */
    memset(longbuf, 'a', 1000000);
    hexdig(CC_SHA1(longbuf, 1000000, d), 20, hex);
    check("sha1-M", hex, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    hexdig(CC_SHA256(longbuf, 1000000, d), 32, hex);
    check("sha256-M", hex,
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    /* offset buffer for md5 (unaligned input through the buffered path) */
    memset(longbuf, 0, 1000);
    for (int i = 0; i < 1000; i++) { longbuf[i] = (char)(i * 7 + 1); }
    hexdig(CC_MD5(longbuf + 1, 999, d), 16, hex);
    check("md5-999", hex, "32fcc41dd07c5374a1e16df551065c3b");

    /* HMAC RFC 2202 / 4231 */
    unsigned char mac[32];
    char mhex[65];
    CCHmac(2, "key", 3, "The quick brown fox jumps over the lazy dog", 43, mac);
    hexdig(mac, 32, mhex);
    check("hmac-sha256-fox", mhex,
        "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
    CCHmac(1, "key", 3, "The quick brown fox jumps over the lazy dog", 43, mac);
    hexdig(mac, 20, mhex);
    check("hmac-sha1-fox", mhex,
        "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9");
    CCHmac(0, "key", 3, "The quick brown fox jumps over the lazy dog", 43, mac);
    hexdig(mac, 16, mhex);
    check("hmac-md5-fox", mhex,
        "80070713463e7749b90c2dc24911e275");
    /* RFC 4231 case 2: key "Jefe", data "what do ya want for nothing?" */
    CCHmac(2, "Jefe", 4, "what do ya want for nothing?", 28, mac);
    hexdig(mac, 32, mhex);
    check("hmac-sha256-jefe", mhex,
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    /* long-key path (>64 bytes hashes the key) */
    memset(longbuf, 0xaa, 131);
    CCHmac(2, longbuf, 131, "Test Using Larger Than Block-Size Key - Hash Key First", 54, mac);
    hexdig(mac, 32, mhex);
    check("hmac-sha256-bigkey", mhex,
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

    if (fails == 0) { printf("all digest vectors pass\n"); }
    return fails != 0;
}
