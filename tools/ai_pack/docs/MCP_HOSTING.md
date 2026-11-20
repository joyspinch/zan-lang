# Hosting the Zan MCP server

The MCP server can host a workspace over stdio or HTTP. Knowledge files are
server-side artifacts: clients do not need local copies of `symbols.json`,
`gallery.json`, or `form.json`.

From the repository root, build the server or generate the source-driven gallery.
Both tools compile the shared Gallery sources. The MCP server also uses the
semantic index emitter shared with `zanindex`:

```sh
build/zanc --stdlib-path stdlib tools/mcp_server/*.zan tools/shared/gallery/*.zan tools/shared/semantic_index/*.zan -o build/zan-mcp
build/zanc --stdlib-path stdlib tools/zanindex/ZanIndex.zan tools/shared/semantic_index/*.zan -o build/zanindex
build/zanc --stdlib-path stdlib tools/genknowledge/GenKnowledge.zan tools/shared/gallery/*.zan -o build/genknowledge
build/genknowledge --root . --stdlib stdlib --out knowledge --seed tools/mcp_server/gallery.json
```

After the server starts, `zan_start_here` reports whether the knowledge
artifacts are present and fresh. A mutable server with the source inputs
advertises `zan_refresh_knowledge`, which regenerates `gallery.json` in process.
It does not build RepoMap or replace `symbols.json`;
generate that index separately when it is missing.

For a shared HTTP deployment, bind to a non-loopback address only with bearer
authentication:

```text
tools/zan-mcp.exe . --host 0.0.0.0 --token-env ZAN_MCP_TOKEN
```

Use `--read-only` for deployments that must not write workspace files.
`--frozen-tools` keeps the advertised catalog stable across workspaces; calls
to unavailable tools still return a diagnostic.

MCP child jobs default to a 120-second timeout and 1 MiB of captured output.
Each server process accepts up to 64 pending jobs and runs up to four at once.
The server requests termination of the managed scope when the direct child
exits, the job times out, or a request is cancelled. A failure to confirm
cleanup sets the job status to `termination-failed`.

Execution defaults to `--sandbox required`. This mode requires Linux x86-64 or
AArch64, Landlock ABI 6 or newer, seccomp, and an unprivileged server identity.
Set `MCP_SANDBOX_CGROUP` to the absolute path of a dedicated cgroup v2 subtree
in the server's delegated domain. The subtree needs `cpu`, `memory`, and
`pids` enabled for its children, and the server must be able to create job
cgroups, move its children into them, and write their limit and kill controls.
The service supervisor should terminate the delegated subtree on server exit.

Each job receives a cgroup with one CPU equivalent, a 1 GiB memory limit, zero
swap, and at most 64 tasks. Landlock grants writes under the configured workspace
and access to `/dev/null`. Use a Linux-native filesystem for writable
workspaces under WSL. Windows-mounted workspaces can fail Landlock write
checks. The policy grants reads for the workspace, configured SDK
packages, standard library, compiler directory, system binary and library
paths, and any compiler response file supplied for that job. Keep the SDK and
compiler outside the workspace when they require read-only access. System calls
that create sockets are denied. Children receive a fixed environment and only
the three standard streams. Host filesystem metadata visibility and service
supervision remain deployment considerations.

A missing backend or prerequisite produces `sandbox-unavailable`. A failure to
establish the policy or start the executable produces `sandbox-start-failed`.
On Windows or macOS, use `--sandbox process` for trusted local execution with
host permissions and Windows Job Object or POSIX process-group cleanup. The
same two modes can be set through `MCP_SANDBOX`, with the command line taking
precedence. `--no-exec` disables shell commands and GUI preview execution.
Compiler tools still use the configured sandbox policy.

A successful HTTP `initialize` returns an `Mcp-Session-Id` header. Send it on
subsequent requests. Use a separate HTTP connection for `notifications/cancelled`
while a tool call is in flight. Cancellation matches the session and JSON-RPC
request ID, including whether the ID is a string or a number. `DELETE` closes
that session and requests cancellation of its active jobs. A deleted or expired
session returns HTTP 404. Sessions expire after two hours of inactivity, and
each server instance holds up to 1024 sessions.

With `--workers`, the master passes its effective configuration to each worker
and all workers share session and active request state. A worker can cancel a
job started on another connection by a different worker. Calls without a session
use stateless HTTP and rely on the process timeout.

The built-in `McpClient` forwards session headers automatically. Use
`await client.CloseAsync()` to send `DELETE`, release local connections, and
reset the handshake. A missing or already expired session counts as closed.
`Close()` and `Dispose()` release local connections.
