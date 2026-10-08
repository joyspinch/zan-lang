# ZanDB single-owner service

`zandb_server.zan` exposes `System.Data.ZanDb.ZanDbServer` over framed TCP. One process opens the database with synchronous durability and owns its file lock. Other processes use `ZanDbClient` or the documented protocol. Every dispatch, including reads and retrieval callbacks that touch caches, runs under one database-wide synchronous monitor. Network IO and shutdown waits happen outside that monitor.

## Configuration and executable

Build the example once from the repository root when the shared compiler is available:

```powershell
New-Item -ItemType Directory -Force _scratch | Out-Null
.\build\zanc.exe examples\db\zandb_server.zan --auto-stdlib -o _scratch\zandb_server.exe
```

Save this JSON as `_scratch/zandb-owner.json`. Replace the example token with your configured token; clients use the same token. Relative paths resolve against the owner's working directory.

```json
{
  "path": "_scratch/zandb-service/owner.zdb",
  "host": "127.0.0.1",
  "port": 19740,
  "token": "replace-this-with-your-local-service-token",
  "timeout_ms": 30000,
  "max_connections": 256,
  "stop_file": "_scratch/zandb-service/stop",
  "client_id": "example-client"
}
```

`path`, `token`, and a nonempty `stop_file` are required in owner mode. `host` defaults to loopback `127.0.0.1`, `port` to `19740`, `timeout_ms` to `30000`, and `max_connections` to `256`. The example accepts ports 1..65535, timeouts 1..600000 ms, and connection limits 1..10000. The token must contain 16..512 UTF-8 bytes. `client_id` is used only in client mode and must remain stable across retries and process restarts.

```powershell
.\_scratch\zandb_server.exe _scratch\zandb-owner.json
```

The owner prints a JSON readiness object after opening the database and starting the listener. A second owner on the same path prints a failed readiness object with a database lock/open error and exits; Windows can reject opening the lock file itself before reaching the explicit lock attempt. Creating the configured `stop_file` requests graceful shutdown: the listener stops admitting requests, idle sockets are woken, admitted dispatches finish their bounded replies, accepted tasks drain, and the database closes. The owner then prints `{"stopped":true}`. Remove the sentinel before starting it again.

The same executable runs a real asynchronous client process:

```powershell
.\_scratch\zandb_server.exe --request _scratch\zandb-owner.json _scratch\zandb-request.json
```

For example, the request file can contain:

```json
{
  "action": "insert",
  "collection": "documents",
  "id": "insert-document-001",
  "document": {
    "title": "alpha document",
    "tag": "public",
    "vector": [1, 0]
  }
}
```

`ZanDbClient` supplies `v`, `token`, and `client`, keeps the explicit write ID, and prints the response envelope. Reads may omit `id`; the client assigns a correlation ID. Executable startup/transport errors are JSON diagnostics; callers should inspect the response/readiness objects because this example's `void Main` does not encode every error as a process exit status.

## Wire format

Each frame has a four-byte unsigned **big-endian** length followed by exactly that many UTF-8 JSON bytes. Length excludes the header and any trailing NUL. It must be 1..8,388,608 bytes for both requests and replies. A connection may carry multiple requests, processed sequentially with one correlated reply each. Fragmented headers/bodies and multiple frames in one TCP write are supported without consuming bytes from the next frame.

Version 1 requires this envelope:

```json
{
  "v": 1,
  "token": "configured-token",
  "client": "stable-client",
  "id": "request-001",
  "action": "get",
  "collection": "documents",
  "document_id": 1
}
```

Successful replies contain the original request ID, committed revision, and action result:

```json
{"v":1,"id":"request-001","ok":true,"revision":7,"result":{"_id":1,"title":"alpha document"}}
```

Errors use the same envelope with `ok:false` and `error:{"code":"...","message":"..."}`. Codes include `invalid_request`, `unsupported_version`, `unauthorized`, `unknown_action`, `invalid_batch`, `not_found`, `id_conflict`, `invalid_state`, `frame_too_large`, `storage_failed`, `internal_error`, and `service_stopped`. Invalid framing may produce one error with an empty ID before connection close; truncated frames and disconnected peers can close without a reply. An unauthorized reply reports revision 0.

Payloads must be JSON objects. Duplicate object members, nesting deeper than 64, invalid UTF-8, and literal NUL bytes are rejected. JSON escaping and serialization use `JsonValue`. Request IDs contain 1..128 ASCII letters, digits, dot, dash, or underscore; client namespaces contain 1..64. Collection/field names use the same characters with a 128-byte bound. Document IDs are integers 1..2147483646.

## Action allowlist

All actions except `ping` and `batch` require `collection`. There is no SQL, executable command, remote callback, or administrative stop action.

| Action | Additional request members | Result |
|---|---|---|
| `ping` | none | service/version object |
| `get` | `document_id` | document or JSON null |
| `query` | optional `filter`, `limit` (default 100), `skip` (default 0) | documents |
| `insert` | object `document` | allocated document ID |
| `upsert`, `update` | `document_id`, object `document` | document ID; missing update returns `not_found` |
| `delete` | `document_id` | whether a document existed |
| `batch` | `operations` array | ordered array of CRUD results |
| `list_indexes` | none | entries with `field`, `type` (`field`/`text`/`vector`), and applicable options |
| `ensure_index` | `field` | true |
| `ensure_text_index` | `field` | true |
| `ensure_vector_index` | `field`, object `options` | true |
| `checkpoint_search` | none | true |
| `rebuild_search` | none | true; rebuilds text/vector generations from source documents |
| `search_text` | `field`, string `query`, optional `k`, `filter` | `{id,score}` hits |
| `search_vector` | `field`, numeric `vector`, optional `k`, `ef`, `filter` | `{id,distance}` hits |
| `search_vector_exact` | `field`, numeric `vector`, optional `k`, `filter` | `{id,distance}` hits |
| `search_hybrid` | `text_field`, `query`, `vector_field`, `vector`, optional `k`, `ef`, `filter` | `{id,score}` hits |

`batch` accepts 1..1000 operations, each an `insert`, `upsert`, `update`, or `delete` with its collection and action members. Its outer ID identifies the complete batch. Nested batches, reads, and index maintenance are rejected before mutation. All steps and the request record commit in one transaction; failure rolls back documents, sequence/index/cache state, and the request record through `ZanDatabase.Rollback()`.

`query` limits are 1..1000 and skip is 0..1,000,000. Retrieval `k` defaults to 10 and is bounded to 1..1000; `ef` defaults to 64 and is bounded to 1..100000. Vector arrays contain 1..65536 finite float32 components and must match the index dimensions. Vector options require `dimensions`; defaults are `metric:0` (cosine; 1 is squared L2), `m:16`, `efConstruction:100`, `efSearch:64`, and `seed:1729`. Supported bounds are `m` 2..128, ef options 1..100000, and seed 0..2147483647.

Vector hits preserve `VectorHit.distance`, with smaller distances ranking first. Text and hybrid hits preserve their relevance `score`, with larger scores ranking first.

Queries and all retrieval actions support one structured equality predicate:

```json
{"filter":{"field":"tag","eq":"public"}}
```

`eq` is a string or a signed 32-bit integer in -2147483647..2147483647. Retrieval compares the document's actual JSON type/value in local `FindById` callbacks **before top-k selection**; strings and integers are not coerced. Vector traversal may cross ineligible nodes to reach eligible results. No remote lambdas are accepted.

## Durable write IDs, revisions, and retention

Each write requires an explicit ID. The durable namespace is:

```text
_service:req:<SHA256(token)>/<client>:<request-id>
```

The record contains a SHA-256 fingerprint and the complete original success response. The fingerprint recursively sorts JSON object keys and excludes `token`, `client`, and `id`; array order and scalar JSON representation remain significant. Duplicate members are rejected. Other request members, including optional/extra fields, remain part of the fingerprint, so keep the body identical for a retry.

CRUD, atomic batches, and equality-index creation persist their record in the same `ZanDatabase.Begin()/Commit()` transaction as the operation. Each successful outer transaction advances the committed revision once; the request-record write makes even an otherwise empty successful write nonempty. Replaying the same namespace/ID/body returns the persisted original result and original revision. A different body with that ID returns `id_conflict`. Failed CRUD batches do not reserve the ID.

Search-index creation, `checkpoint_search`, and `rebuild_search` operate on committed state under the same owner monitor, with bounded staging commits and an atomic published generation. They run without a service outer transaction, then persist the request record in a final transaction. Their success revision is the revision after that final record commit. A crash before the record is durable can repeat maintenance; same-config Ensure, checkpoints, and source rebuilds are retry-safe. Published old/completed generations remain valid. Maintenance is excluded from CRUD batches, and its staging commits can advance more than one revision.

`rebuild_search` rebuilds the collection's existing text/vector definitions from its source documents, allowing the file-owning service to repair unusable checkpoints. The matching client method is `RebuildSearchIndexesAsync(collection, requestId)`. Same-config Ensure preserves an existing generation; use an explicit source rebuild when it needs repair. Repeating a successful rebuild with its original ID returns the durable result; use a fresh write ID to request another rebuild after later data changes.

Request records are retained indefinitely. This preserves retry behavior across arbitrary owner/client downtime, at the cost of growth proportional to distinct successful write IDs and their serialized responses. There is no automatic TTL, pruning, or remote deletion action: deleting a record would permit that old ID to execute again. Stable client IDs and tokens preserve the namespace. Changing either creates a new namespace; a write repeated there can execute again.

A lost connection or deadline does not establish whether a write committed. Retry with the same client/token/ID and identical body. `SendAsync` performs one attempt. `RetryWriteAsync(request,id,attempts,cancellation)` explicitly permits 1..5 attempts and retries transport IO/deadline failures with that same ID; it returns protocol error envelopes without retrying. Storage failures stop the owner and require recovery before replay.

## Library lifecycle and timeouts

Construct `ZanDbServerOptions`, set `path`/`token` and any optional listener settings, then create `ZanDbServer`, call `Start()`, and run `RunAsync()`. `RequestStop()` requests shutdown; await `StopAsync()` (also exposed as async `Stop()`) to drain IO tasks and close the owner. `Execute(JsonValue)` is the gated synchronous entry point used by the in-process conformance test. The database is private to the server.

`ZanDbClient(host,port,token,clientId)` provides `SendAsync(JsonValue)` and a cancellation-token overload, explicit-ID CRUD/index/batch methods, query/get methods, and text/vector/exact/hybrid methods with optional `JsonValue` filter overloads. Concurrent calls own separate connections, so frames never interleave. `Close()` wakes active IO; await `CloseAsync()` to finish their cleanup. `Dispose()` requests the same close.

Frame reads and writes use exact logical byte counts and absolute monotonic deadlines. A 10 ms timer checks deadline/cancellation and shuts down pending socket IO; the owner closes the descriptor after the awaited operation and timer state finish. Server deadlines apply separately to each frame read and reply write. Client IO shares an overall transport deadline. The current `TcpClient.ConnectAsync` API does not accept cancellation; cancellation is checked before/after connect, and DNS/connect is governed by that API's timeout behavior. Cancellation cannot roll back an already admitted synchronous server operation.

## Verification

`tests/conformance/zandb_service.zan` exercises synchronous dispatch, rollback/cache recovery, canonical replay, revisions, all index types, retrieval filtering before top-k, owner reopen, and explicit source rebuild/replay. Its `.out` file is the expected deterministic output.

The persistent integration test requires Python 3.9+ and an already built example:

```powershell
python tests\integration\zandb_service_test.py --exe _scratch\zandb_server.exe
```

It launches a real owner, two concurrent ZanDbClient executable processes, two Python client workers, and a reader. It checks same-ID concurrency/conflict, complete-pair snapshots, rollback, exclusive owner rejection, fragmented/coalesced/malformed/truncated frames, an idle deadline, lost-reply replay, kill/restart transaction recovery, persisted retrieval indexes, checkpoint/source-rebuild replay, and graceful stop with fragmented clients and idle accept. It creates its sandbox under `_scratch`, kills only its own child processes, and removes its files on exit. It does not build the compiler or server.

Both layers were executed on Windows 10 with `--check-leaks` instrumented children: the 32-line service golden and all 12 multiprocess check groups pass, graceful stop drains cleanly, and no owner/client output contains a `memory leak detected` report. `tests/conformance/tcp_listener_cancel` additionally pins idle-accept cancellation, retained-descriptor stop/reuse races, no implicit restart after stop, and explicit restart; `tests/conformance/timer_stop_drain` pins periodic callbacks, callback self-stop/restart, and leak-free `Timer.StopAsync` exit. POSIX accept/cancellation paths in `Socket`/`TcpListener` were static review only in this round and are not claimed as runtime-verified.
