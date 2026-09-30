# Zan.Security

Pure-Zan cryptography, extracted from the standard library. Namespaces are
unchanged, so existing programs compile as-is once the package is visible
to zanc (project `packages/`, `.zan-packages/`, or a toolchain-relative
`packages/` store).

`System.Security.Cryptography` — symmetric (AES, AES-GCM, SM4), hashes
(SHA-1/256/512, MD5, SM3), MAC/KDF (HMAC, HKDF, PBKDF2), public key
(RSA, ECDSA, Curve25519/x25519, SM2, arbitrary-precision BigInt),
encodings (Base64, Hex, Crc32C), X.509 certificate parsing, JWT, and OTP.
Everything is managed Zan — no native crypto library is linked.
`System.Security.Guard` — integrity/tamper-check helper.

Consumers span the ecosystem: Zan.Data drivers (MySQL/Firebird/TDengine
auth), Zan.AppUpdate (signed releases), Zan.Commercial (licenses), and the
stdlib's own `System.Net.Tls` managed TLS stack, which references these
namespaces and pulls them from the package store on demand. Programs that
never reference them compile without this package. `RandomNumberGenerator`
stays in the stdlib root namespace on purpose — `Guid` needs it, and
`using System;` must not drag the crypto suite along.
