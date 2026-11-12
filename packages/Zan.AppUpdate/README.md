# Zan.AppUpdate

Authenticated ZIP release installation and integrity recovery for Zan applications.

`ReleasePackager.Pack` creates a ZIP and an **unsigned** manifest containing package size,
`packageSha256`, and each file's size and `sha256`. MD5 fields remain diagnostic metadata;
installation and recovery never use them as a security check.

The publisher signs the complete canonical `manifest.SigningPayload()` using
RSASSA-PKCS1-v1_5/SHA-256. Signing changes the in-memory manifest; save it afterwards:

```zan
UpdateManifest manifest = ReleasePackager.Pack(
    releaseDir, zipPath, manifestPath, "MyApp", "2.0.0", "Release notes", packageUrl, null);
if (manifest == null) { return 1; }
if (ReleasePackager.SignManifest(manifest, publisherModulusHex, privateExponentHex).Length == 0) {
    return 1;
}
manifest.SaveToFile(manifestPath);
```

Private keys belong only in the publisher's protected signing process. A real client
ships its trusted public key independently of downloaded data:

```zan
AppUpdater.SetTrustedSigner(installedPublisherModulusHex, "010001");
bool installed = AppUpdater.UpdateTo(
    currentVersion, downloadedManifestPath, downloadedZipPath, applicationDirectory, true, null);
```

Missing or malformed trust anchors, missing/invalid signatures, and manifests without
SHA-256 bindings fail closed. Invalid signer configuration clears the previous anchor.
RSA moduli must be 2048–4096 bits with the high bit set and odd, and exponents must be odd
and at least 3. Signer fields are private; configure once before starting concurrent work.
A manifest claiming no newer version is still authenticated before returning success.

For public-key rotation use `SetTrustedSigners("nHex:eHex\nnHex:eHex")` or
`SetTrustedSignerPemBundle(localPublicPemBundle)`. Both replace the complete trust set,
accept at most five distinct public keys, and clear trust on any invalid entry. The old
`SetTrustedSigner` replaces the set with one key. Keep the current and previous public
keys during overlap; remove the previous key to revoke it. Signing retains only the
active private key. Manifest bytes and hexadecimal signatures are unchanged.

The IDE reads `ZAN_UPDATE_PUBLIC_KEYSET_FILE` (a local public PEM bundle), falling back to
`ZAN_UPDATE_PUBLIC_KEY_FILE` only when the keyset variable is absent. An invalid configured
keyset fails closed. These files must be provisioned independently of pending updates;
the bootstrap resolves their paths before changing its working directory.

For direct installation use the authenticated overload:

```zan
bool installed = AppUpdater.ApplyPackage(zipPath, manifest, applicationDirectory, true, null);
IntegrityReport report = AppUpdater.VerifyAndSelfHeal(
    applicationDirectory, manifest, zipPath, null);
```

`TamperDetector.Check(directory, manifest, callback)` requires authenticated metadata.
Null/invalid/untrusted manifests produce a non-clean report. Recovery is also available
as `TamperDetector.RestoreFromZip(directory, manifest, report, zipPath, callback)`.
The mutable report is output, never installation authority. Recovery authenticates and
reinstalls the complete package, then checks disk contents again. It returns the number
of previously damaged/missing files recovered only when the final check succeeds.
The old unsigned `ApplyPackage` and report-only recovery overloads have been removed.
`VerifyZipPackage` is a diagnostic SHA-256 comparison and does not authorize installation.

Every archive entry must match exactly one signed file path, size and SHA-256 digest.
All entries are extracted and checked before the first destination write. Duplicate
case aliases, file/directory collisions, absolute/traversal paths, dot/empty segments,
Windows device names, alternate data streams, trailing dots/spaces and existing reparse
ancestors are rejected (POSIX symbolic links, including dangling links, are checked
at the leaf and each ancestor). Explicit ZIP directory entries and methods other than stored
or deflate are unsupported. Local headers must agree with central-directory metadata;
encrypted entries and fallback local-header scanning are rejected.

Limits are 8 MiB manifest text, 10,000 files, 64 MiB per expanded file, 256 MiB total
expanded content and 256 MiB package bytes. These limits apply before extraction.
The ZIP decoder also checks CRC and bounds its deflate expansion.

Installation takes a nonblocking exclusive OS file lock at `_appupdate.lock`; another
installer returns `false`. It stages every validated new file and immutable snapshots of
existing destination files before publishing. The root-bound, checksum-protected local
journal records the exact expected signed manifest and the old file/backup hashes.
On Windows, the root binding uses the actual directory's volume/file identity so case
and short-name aliases can retry the same installation without conflating distinct
case-sensitive directories. POSIX root normalization preserves literal backslashes;
only `/` is a directory separator. State copied to a different Windows directory is
rejected. Transaction format 3 preserves this root binding and adds ordered persistence
barriers. Retained format-2 state remains recoverable under its original process-interruption
contract; its earlier writes cannot retrospectively acquire a power-loss guarantee.
Version-1 path-bound state is rejected without deleting it; finish that older in-flight
transaction using its compatible updater before installing with this version.
Publication uses individual same-volume renames into the existing application layout.
An I/O failure triggers reverse rollback, including removal of newly created files.
This provides a recoverable transaction, not instantaneous atomic visibility of the
whole directory: stop application activity during installation. Empty directories created
during a failed attempt may remain.

On the next `ApplyPackage` call, a pending publishing transaction is rolled back before
another installation starts. **Retain and retry the exact signed manifest and matching
ZIP used by the interrupted attempt.** A different expected manifest/package is rejected
while rollback is pending; a retained older signed manifest never independently authorizes
rollback. Original snapshots remain intact during rollback so an interrupted rollback can
be retried, including when a destination was restored before restoring its caller
backup failed. Committed cleanup can also resume after some snapshots or the
journal have already been removed. Corrupt journals, changed snapshot/destination bytes, unsafe paths, and failed
rollback leave the transaction state in place and return `false`; restore trusted local
state before retrying. A committed transaction is cleanup-only and can never trigger
rollback. Once the commit barriers succeed, installation returns `true` even when cleanup
must be retried on a later installation. A visible `committed` rename whose barrier failed
returns `false` and retains recovery material; recovery confirms the marker barriers before
cleanup. Keep the exact inputs until recovery resolves this uncertain result. Format 3
writes a synchronized `preparing` marker before staging; interrupted preparation has not
changed payload files and only its bounded flat staging inventory is cleaned.

Transaction state is private to `_appupdate`, separate from the caller-facing `_backup`.
With `backupOld=true`, success replaces `_backup/<path>` with the pre-install destination
where one existed, preserving the existing API behavior. A failed install restores any
preexisting caller backup and removes backups newly created by that attempt. Rollback
snapshots are always kept until commit, even with `backupOld=false`. Releases cannot use
the top-level `_backup` subtree, or `_appupdate` / `_appupdate.lock` as any path segment,
including nested installation roots' state and lock files. These reservations compare
case-insensitively on every platform.
Existing destination and caller backup files are conservatively limited to 64 MiB each;
new payload plus old destination/backup snapshots are limited to 768 MiB in total.
Oversized snapshots fail before payload publication. Journals are limited to 8 MiB.

Format 3 orders preparation, publication, rollback and cleanup through `File.FlushToDisk`
and, on POSIX, `Directory.FlushToDisk`. Snapshot files and journal entries are synchronized
before publication; replacement files and directory entries precede the commit marker.
Matching rollback bytes still require mode restoration and synchronization. Cleanup synchronizes
inventory deletion before removing the phase, then synchronizes phase and state-directory
removal. Any required POSIX barrier failure blocks progress and can be retried. Creating
new parent directories also synchronizes their ancestor chain.

Windows uses file barriers only: directory synchronization remains unsupported, so durable
namespace publication is not established there. Matching read-only original files on Windows
are left untouched; they do not prevent rollback of earlier replacements. WASM persistence
barriers are unsupported. Filesystem/device behavior and real power-loss recovery require
separate acceptance; software fault injection or process interruption does not establish that
result. The application directory and every ancestor,
including transaction state and the lock file, must be trusted local storage with write
access controlled as strictly as the installed executables. Journal checksums detect
corruption and are not authentication against an actor able to rewrite that trusted
storage. Recovery derives write paths only from the caller's authenticated expected plan;
transaction slots use fixed numeric names. Reparse ancestry checks and flat cleanup avoid
following links, but cannot prevent simultaneous hostile directory mutation by a local
writer. Progress callbacks run after security-sensitive work so they cannot alter
authenticated metadata during installation.

The signed format now includes `packageSha256` and per-file `sha256`; regenerate and
re-sign older manifests. Existing signatures over the old MD5-only payload are rejected.
`TamperFileIssue.expectedSha256` / `actualSha256` and report summaries expose SHA-256.

`examples/QuickStart.zan` combines a publisher and client for demonstration. Configure a
**disposable** RSA key with `ZAN_APPUPDATE_DEMO_MODULUS` and
`ZAN_APPUPDATE_DEMO_PRIVATE_EXPONENT` (big-endian hex, exponent 65537), then run it from the
repository root. It writes under `_scratch/appupdate_demo`. Never configure a production
private key for this demo. The conformance test uses a public test-only key fixture.


## Windows loaded-image updates

A loaded Windows executable cannot replace itself. Dispatch
`UpdateBootstrap.Handle(args, HostApply)` at the very start of `Main(string[] args)`,
before GUI registration, worker startup or other application initialization. A return
value of `-1` means ordinary startup; every other value is the helper exit code.

After independently configuring the installed signer and checking the product/channel,
call `UpdateBootstrap.Begin(targetRoot, pendingRoot, workRoot, manifest, relaunchArgs)`.
The current executable must be directly inside `targetRoot` and included in the signed
manifest. Pending staging contains `manifest.json` and `release.zip`. `workRoot` must
be outside the installation and on a trusted local filesystem; staging may be in a
configuration subtree inside the installation. The work directory and installed files
must not be writable by less trusted users. The request checksum binds a snapshot; it
is not an authorization token against another process with the same user's write access.

This bootstrap supports native hosts whose non-system DLL dependencies are deployed
beside their executable. It copies the trusted installed executable and adjacent DLLs
into a random work subdirectory, launches that copy without a shell, opens the exact
original parent using PID, creation time and image path, then acknowledges readiness.
Only a `true` result authorizes the caller to return normally from Main; a `false`
result must preserve staging and report failure. The parent has a 30-second ready
limit. The helper waits up to two minutes for the opened parent handle to signal exit.

The `BootstrapApply` callback receives `(targetRoot, pendingRoot, manifestSha256,
install)`. With `install=false`, it must independently configure the trusted signer,
check host-owned product/platform policy and fixed staging location, authenticate the
bound manifest and archive, and return a positive value without installing. It runs
before the ready handshake. With `install=true`, it must repeat those checks and use
`AppUpdater.ApplyPackage` plus the host's existing managed inventory bookkeeping;
return a positive value only after full success. Never choose a key from request data.
The target is bound to the opened parent's original executable path. If host policy
also configures an installation root, check that independently in the callback.

After parent exit, the helper rechecks the request and pending snapshot, invokes the
callback, checks the complete installed signed inventory and rejects adjacent DLLs
missing from that inventory. The release must therefore include the complete native
DLL deployment. It relaunches only the fixed original host path with bounded arguments.
It never launches after a failed application or verification. Failed pending data is
left for inspection/retry by the host's application logic. Relaunch failure after a
successful commit is reported separately and does not roll back a completed install.

`result.json` contains a fixed status without paths, keys or exception bodies. On a
later Begin, cleanup considers at most 32 terminal work directories with at most 264
flat files each. It deletes the helper executable first; Windows rejects deletion of
a loaded image, leaving all its dependencies untouched. In-progress handoffs are
preserved. Abruptly interrupted helpers without a terminal marker may require local
inspection. Non-Windows helper dispatch returns 40 and Begin returns false; hosts must
provide an explicit unsupported-platform result rather than applying a loaded image.
