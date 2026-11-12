# Package security changes and API migration

This migration guide describes the package changes and their remaining limits as
of 2026-11-12. It covers the interfaces and behavior described below.
Executed independent-service results and their remaining coverage limits are recorded
in [package acceptance evidence](PACKAGES_ACCEPTANCE.md).

## HTTP client configuration and content decoding

Configure `HttpClient` transport security and `ExternalCallPolicy` before its first
use. `UseTls`, `DisableTlsVerify`, `PinPublicKey`, `DisablePinning`, `SetCertPolicy`
and `SetCallPolicy` reject changes after first use with `InvalidOperationException`.
First use includes failed requests, direct file-download attempts, and creating a
client TLS context. `Close()` does not reopen configuration. Create a new client
when its security policy must change.

The freeze orders setters against first use with a mutex, so an idle pooled
connection cannot survive a change to its authentication policy. It does not make
all client state or arbitrary concurrent requests thread-safe. `SetCallPolicy`
stores a copy, and `CallPolicy()` returns a copy; modifying either the original
policy or the returned object does not reconfigure the client.

The unbounded `HttpContentDecoder`, `SetContentDecoder` and
`SetDefaultContentDecoder` APIs have been removed. Use:

```zan
http.SetBoundedContentDecoder(ContentCoding.DecodeBounded);
// Optional application-wide default:
HttpClient.SetDefaultBoundedContentDecoder(ContentCoding.DecodeBounded);
```

Import `System.Net.Http.Client` and `System.IO.Compression` for these types. A custom
`HttpBoundedContentDecoder` has signature
`byte[] (string encoding, byte[] data, int maxDecodedBytes)`. It must enforce the
limit **during** decoding, before allocating or writing output beyond the budget;
checking only the final output is insufficient. Return null or throw on failure.
HTTP supplies `ExternalCallPolicy.MaxResponseBytes()` (64 MiB fallback without a
policy) to every decoded layer and also checks returned lengths. Text and binary
response paths preserve embedded NUL bytes using explicit lengths. HTTP/1 text and
byte exchanges share response framing and apply the response limit to the body;
response headers have their own bound. An encoded, nonempty response without a
bounded decoder fails explicitly. File-download APIs reject encoded content
rather than automatically decoding it.

`ContentCoding.DecodeBounded` bounds gzip/deflate expansion, including the aggregate
output of concatenated gzip members. Budgeting custom application callbacks remains
the callback author's responsibility.

Implementation: [HTTP client](../packages/Zan.Net/src/System/Net/Http/Client/HttpClient.zan),
[security/decoding](../packages/Zan.Net/src/System/Net/Http/Client/HttpClient.Security.zan),
[content coding](../packages/Zan.IO/src/System/IO/Compression/ContentCoding.zan).

## TLS, OCSP and WebSocket limits

TLS client completion enforces handshake identity and state independently of
`DisableVerify`. TLS 1.3 requires EncryptedExtensions, Certificate and a verified
CertificateVerify, the expected handshake keys/ciphers and successful policy
results before Finished. TLS 1.2 requires the authenticated signed key exchange,
master secret, peer ChangeCipherSpec and expected Finished state. An out-of-order
or omitted identity flight cannot complete authentication or skip configured pins
and required-OCSP refusal. This implementation does not negotiate PSK-only handshakes.

`HttpClient.SetClientCertificate` and `TlsContext.CreateClientWithCertificate`
throw `NotSupportedException`: TLS client certificate authentication (mTLS) is not
implemented. Existing method signatures do not imply client credentials are sent.
WeChat payment V2 operations requiring mTLS remain unavailable; see the
[WeChat package limitation](../packages/Zan.Sdk.Wechat/README.md).
The missing implementation includes CertificateRequest handling and client
Certificate/CertificateVerify transcript sequencing in TLS 1.2 and 1.3. Existing
server-side signing primitives do not establish client-authentication support.
Credential support also requires strict supported-key-format validation and
certificate/private-key identity matching before enabling these public APIs.

`OcspResponse.Parse` is a diagnostic DER parser. It does not validate the response
signature, responder authorization, certificate identity (`CertID`) or freshness.
Parsed Good, Revoked and Unknown values are untrusted. Optional staples do not
provide revocation assurance. Setting `TlsEngine.requireOcspStapling` rejects
certificate verification with an explicit unsupported-validation error
(`verifyResult` 64), even with `disableVerify` or a certificate-policy callback.
There is no supported required-OCSP validation mode in this implementation.
A complete verifier must match issuer-name/key hashes and the leaf serial, verify
the exact signed response bytes, authorize an issuer or delegated OCSP signer,
select the matching entry and enforce freshness. The current parser and TLS chain
interfaces do not supply all of these authenticated inputs.

WS and WSS client/server handshake headers are limited to 65,536 bytes including
the terminating `CRLFCRLF`. Frame bytes coalesced after that terminator do not count
against the header budget. The WS and WSS clients share frame validation,
message assembly, send serialization and close handling. Each transport still
establishes its own connection; WSS uses TLS certificate verification.

`WsReader.Ensure` rejects invalid or overlimit capacity requests before explicit
length copies, rather than silently shrinking the requested allocation. The buffer
budget includes the configured frame limit plus 65,536 bytes of receive slack;
invalid negative or overflowing budgets fail explicitly.

WS/WSS client `CloseNow` stops new transport operations and shuts down the socket
immediately. Physical TCP/TLS disposal waits for both an admitted receive and any
admitted send to exit, retaining the numeric descriptor across readiness waits
and preventing an old operation from using a replacement connection's handle.
`CloseNow` remains synchronous; completion of resource disposal can be deferred
until those operations drain. Raw caller-owned `WsReader` transports require their
owner to retain the descriptor across pending operations as well.

## ORM expressions and transaction failure

`ExprSql` validates the supported expression tree before rendering/binding it.
Missing nodes, unsupported operators/calls and unsupported constant forms throw
`NotSupportedException`; they no longer become an unconditional `1=1` predicate.
`Select.WExpr`, `Update.WExpr` and `Delete.WExpr` also resolve member names against
entity metadata and reject unknown columns. Handle translation failure by rejecting
the operation; do not retry without the predicate.

The detached `InI`, `InL`, `InS` and `InD` parameter-only helpers were removed from
these builders. Use supported `WhereIn(column, values)` overloads, which build the
condition and its parameters together. Raw SQL fragment APIs remain trusted-code
interfaces: this validation does not sanitize arbitrary SQL passed to them.

Explicit delegate predicates run after fetching the complete SQL candidate set.
They no longer silently truncate sparse matches before pagination. Use bound
SQL predicates for large or externally driven searches to avoid materializing
an excessive result set. List, single-row, paging, projection and related-row
async reads use the driver's async query contract. Query terminals take a snapshot
of builder state and bound parameters when execution starts. These snapshots
isolate builder changes; they do not provide database transaction isolation or
make captured delegate state immutable.

For UPDATE and DELETE, delegate `Where` calls also defer candidate reads until
execution. Async terminals await `QueryAsync`, preserve bound SQL conditions and
intersect all delegate predicates. Matching keys are bound into the mutation;
zero matches generate `1 = 0`, and composite-key alternatives are grouped beneath
the original SQL condition. Candidate reads and writes remain separate statements.
Use transaction isolation appropriate to the operation when concurrent changes
between those statements matter. These APIs do not make arbitrary client-side
predicate mutations atomic.

The retired `Model` and generic `Dao<T>` entry points have been removed. See the
[Zan.Data migration guide](../packages/Zan.Data/README.md#migrating-from-the-legacy-orm)
for typed queries, runtime tables and repository replacements.

`UnitOfWork.CreateAsync` checks the result of `BeginTransactionAsync` and closes/drops
the connection when BEGIN returns false or throws. Synchronous constructors use the
void `BeginTransaction` API: when it throws, they discard the uncertain connection
and preserve the original exception, including when connection cleanup also fails.
Savepoint commands check negative execution results and propagate exceptions. `RollbackAsync` closes/drops the connection on
false or exception; synchronous `Rollback` does so on exception. These checks depend
on the driver reporting failure through its result or exception. A failed
`CommitAsync()` does not mark the transaction committed. Callers must check return
values and handle transaction errors; these changes do not provide distributed
transactions or automatically make failed business operations safe to replay.

`TransactionScope` records one terminal commit/rollback attempt. Failed async
completion returns false, does not mark the commit successful and is not implicitly
retried by `Complete` or disposal. `DisposeAsync` reports the recorded result,
including on repeated calls; underlying `IUnitOfWork.Dispose` runs once and owns
its own unfinished-transaction cleanup. A transaction exception takes precedence
over a cleanup exception, while cleanup-only exceptions propagate. Use a scope
from one serialized transaction flow; overlapping completion and disposal are not
supported by this contract.

Implementation: [expression translator](../packages/Zan.Data/src/System/Data/Orm/ExprSql.zan),
[transaction ownership](../packages/Zan.Data/src/System/Data/Orm/UnitOfWork.zan).

Generic pool release and shutdown release accounting in `finally`, even when a
connection's physical close throws. Shutdown drains remaining idle connections
and wakes waiters before rethrowing its first cleanup exception. Failed warmup
acquisition returns all earlier borrows; a failed return still releases the
remaining tail without masking the first exception or retrying that entry. Firebird cleanup
also checks the rollback/reset boolean: false or exception evicts the session,
including when TCP remains connected but the replacement transaction failed.

## ZanDb persistence and backup boundaries

KV store open confirms the WAL parent-directory barrier on supported POSIX
backends; a failure rejects open and releases both writer guards. The caller owns
persistence of any parent directory chain it creates. Windows preserves existing
operation compatibility but has no supported directory barrier, so file flushing
alone does not establish durable namespace publication.

RecordTable checks WAL append, synchronization, replay writes and tail repair.
Blob bytes and their header synchronize before publishing a WAL frame that refers
to them. Checkpoint resets the recovery log only after table, blob and index
writes and synchronization succeed. The first I/O error blocks further writes;
explicit mutations and Flush propagate IOException. Close retains its error and
releases resources without throwing, allowing facade cleanup to continue. A
failed commit may already be present in the WAL: reopen and inspect before
retrying. These are table-local transactions, without an atomic transaction
across KV, record and vector engines.

Backup checkpoints all KV frames and checks that both the WAL and pending buffer
are empty before creating a new target WAL. It keeps source writer locks held;
reopening the Windows source WAL for copying would conflict with its byte lock.
Copied segments and sidecars synchronize before directory publication, and
verification-close writes receive a final synchronization. Copy, synchronization
and verification failures propagate and clean generated incomplete target files.
Use a separate trusted target: overwriting does not preserve an older backup or
provide an atomic multi-file replacement. Software ordering and fault injection
are not acceptance evidence for actual power loss or device-cache behavior.

## Database wire receive boundaries

MySQL sync and async readers cap a logical response message at 64 MiB of payload,
including continuation packets and excluding headers. The same limit is advertised
in login and TLS-upgrade requests. Header admission uses subtraction before size
addition or payload allocation. Oversize and truncated messages close the session;
a partial continuation cannot become a successful response. This is a message
budget, not a bound on all rows accumulated across a query.

Redis RESP line content is limited to 65,536 bytes including the type prefix and
excluding CRLF. The parser bounds receive growth before reading, validates both
bulk trailer bytes, rejects malformed/overflowing signed Int64 headers, and allows
64 array nesting levels. Existing per-bulk (512 MiB), per-array (1,048,576 elements)
and receive-buffer (1 GiB) limits remain. Malformed, truncated and overlimit replies
close the session and retain a protocol error; later commands cannot reuse it.
Ordinary server error replies do not close a valid session. Each root response now
shares a default 64 MiB logical-payload budget and 1,048,576-node budget across
nested arrays. Bulk/simple/error values count wire bytes and integers count eight
bytes; arrays, scalars and nil count as nodes. Headers reserve direct child nodes
and bulk bytes before their allocations or reads. Positive per-client limits can
be configured with `SetReplyLimits` before commands; pooled facades apply and
restore the physical client's settings per borrowed command. These are logical
payload/node limits, not an exact bound on process RSS or allocator overhead.

## Signed updates and installation boundaries

File helpers used for transaction records now check final buffered-close failures,
and `File.Copy` checks source read errors as well as destination close. A short
write or delayed OS error therefore cannot be reported as a successful journal or
snapshot write. This improves ordinary I/O failure handling; it does not add file
or directory persistence barriers and does not extend the power-loss guarantee.

Installation and recovery require an authenticated manifest and a separately
provisioned trusted publisher public key. `ReleasePackager.Pack` produces an
**unsigned** manifest: sign it with `ReleasePackager.SignManifest`, then save the
signed manifest. Configure the client with `AppUpdater.SetTrustedSigner` before
concurrent update work. Do not obtain the trust anchor from the downloaded release.

Use `AppUpdater.ApplyPackage(zipPath, manifest, directory, backup, callback)` or
`UpdateTo`, and manifest-bearing recovery APIs. The unsigned installation and
report-only recovery overloads were removed. `VerifyZipPackage` alone is diagnostic
and does not authorize installation. Old MD5-only manifests must be regenerated and
re-signed: package and file sizes/SHA-256 values are covered by the canonical RSA
PKCS#1 v1.5/SHA-256 signature. MD5 metadata is not installation authority.

All ZIP entries are validated and expanded content checked before destination
writes. Current limits are 8 MiB manifest text, 10,000 files, 64 MiB per expanded
file, 256 MiB total expansion and 256 MiB package bytes. Unsupported archive forms,
unsafe paths, duplicates/collisions and existing reparse ancestors are rejected.

Installation now holds an exclusive per-directory `_appupdate.lock` and stages
all new bytes plus original destination/backup snapshots before publication.
An I/O failure triggers reverse rollback, restoring original files and caller
backups and removing newly introduced files. Interrupted publishing is recovered
on the next `ApplyPackage` call using the **same signed manifest and matching ZIP**.
Retain those inputs until recovery finishes. A mismatched expected plan, corrupt
journal/snapshot, unrelated destination changes or failed rollback blocks further
installation and retains recovery material. Partial rollback remains retryable
when a destination is already restored but restoring its caller backup fails.
Committed state is cleanup-only, including after partial snapshot/journal cleanup.
The reserved `_appupdate` and `_appupdate.lock` names are forbidden in every
manifest path segment, so a parent installation cannot replace a nested
installation's transaction state or lock. Caller-facing `_backup` retains its
separate top-level rules.

Transaction formats 2 and 3 bind Windows state to the directory's volume serial and
file ID rather than its path spelling. Equivalent directory spellings can recover
the same installation; moving state to a different directory is rejected. POSIX
root normalization strips only trailing `/`, preserving a literal trailing `\`
in a directory name. Legacy format 1 state is rejected and retained: complete its
recovery with a compatible updater before using this format.

This is a recoverable transaction with individual file renames; readers can still
observe publication in progress. Stop application activity during installation.
Format 3 uses file barriers and POSIX directory barriers to order preparation,
publication, commit, rollback and cleanup. A synchronized `preparing` marker precedes
staging. Snapshot files and the journal precede `publishing`; synchronized replacements
precede `committed`. A failed commit barrier returns `false` with recovery material
retained even if the marker rename is visible. Recovery confirms that marker before
cleanup, and synchronizes rollback even when bytes already match. Inventory deletion
is synchronized before phase deletion; phase deletion precedes synchronized removal
of the state directory. Required POSIX barrier failures propagate and leave retryable
state. Each transaction owns its persistence collaborator; fault injection uses no
global callback or public bypass.

Windows has file barriers but no directory barrier backend, so namespace durability
remains unsupported there. Its matching read-only originals are not rewritten or flushed
with write access during rollback. WASM barriers are unsupported. Format 2 retains its
original process-interruption contract; its unsynchronized history is not retroactively
made power-loss safe. Actual power-loss recovery is unverified on every platform;
software barrier injection establishes ordering/error handling only. Empty directories
created by a failed attempt may remain. Existing destination and caller backup files are
limited to 64 MiB each; staged new/original/backup bytes are limited to 768 MiB total.

The installation directory and its ancestors, transaction state and lock must be
trusted local storage. Journal checksums detect corruption, not hostile rewriting
by someone who can also modify installed executables. Recovery derives write paths
from authenticated caller metadata and validates reparse ancestry. On POSIX,
`File.IsReparsePoint` checks symbolic links, including dangling links; the updater
checks each ancestor as well as the leaf. These path checks cannot defeat
concurrent hostile directory replacement. Signed hash/MD5 text is
preserved during snapshot and JSON parsing so a save/load round trip does not
change signature input; hash comparisons normalize case separately.
See the [AppUpdate README](../packages/Zan.AppUpdate/README.md) for signing,
recovery, supported ZIP forms and operational details.

## HTTP/2 and HPACK supported subset

These connections implement a bounded subset, not the complete HTTP/2 feature set.
Incoming frame payloads are capped at 16 KiB before peer-sized allocation. Header
blocks require contiguous CONTINUATION frames and allow at most 16 fragments.
The current header budget is at most 64 KiB, with 128 decoded fields and HPACK list
accounting that includes the per-field overhead. Invalid indices, integer/string
encodings and prohibited dynamic-table size updates are rejected.

Client defaults are 16 concurrent streams, 8 MiB per response and a fixed 8 MiB
aggregate buffered-response budget; requests are capped at 8 MiB. Server defaults
and configuration ceilings are 16 pending streams, 8 MiB request/aggregate buffered
body budget and 64 KiB headers. The server also bounds buffered responses using its
body budget. Both default to a 30-second timeout; exposed configuration is not a
promise of unbounded operation.

Server push, CONNECT, trailers and informational (1xx) responses are unsupported.
Invalid pseudo-header ordering/duplicates, connection-specific headers, content
length mismatches and flow-control violations are rejected. Applications needing
those features must use another supported transport rather than assume transparent
fallback. See [client](../packages/Zan.Net/src/System/Net/Http/Http2/Http2ClientConnection.zan),
[server](../packages/Zan.Net/src/System/Net/Http/Http2/Http2ServerConnection.zan) and
[header validation](../packages/Zan.Net/src/System/Net/Http/Http2/Http2HeaderBlock.zan).

## Upload spool ownership

HTTP/1 routes marked `StreamUpload` use the same fixed-length spool path over
TCP and TLS. Limits and route authentication run before opening a file. Incomplete
uploads are removed; bytes belonging to a subsequent request stay in the framer.

A completed streamed-upload spool is owned by its `HttpContext`. Request cleanup
removes it, including when hooks or response sending fail. `UploadPath()` only
observes the path; it does not retain ownership. A handler that needs the original
file after the request must call `TakeUpload()`, which returns the path and clears
the context's upload path/size. The caller then owns moving or deleting that file.
Read `UploadSize()` before the transfer if needed. Cleanup deletion failures are
logged; automatic cleanup is not a guarantee of deletion against filesystem errors.

Implementation: [HttpContext](../packages/Zan.Web/src/System/Web/Http/HttpContext.zan).

## Credential compatibility

MVC new-user creation uses `AuthUser.NewHash`, the self-describing
`pbkdf2$<iterations>$<saltHex>$<hashHex>` format with PBKDF2-HMAC-SHA256 and a random
16-byte salt. Iterations follow authentication configuration (300,000 fallback).
Do not write new credentials with `AuthUser.PasswordHash`.

The legacy salted hash algorithm remains readable for existing credentials.
`PasswordHash` now sizes its temporary buffer for the actual password/salt material
with overflow checks, preserving existing hash output rather than imposing a new
password-length limit. `AuthUser.Verify` retains both stored formats. The login path
attempts to upgrade a successfully verified legacy credential to the PBKDF2 format;
this does not change the token version. This compatibility repair is not a claim
that the legacy algorithm is suitable for new credentials, nor a forced reset of
existing accounts.

Implementation: [AuthUser](../packages/Zan.Mvc/src/Zan/Web/Framework/Security/Models/AuthUser.zan).
