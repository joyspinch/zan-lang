/* rpc.h -- Content-Length framed message transport over stdio.
 *
 * Both the Language Server Protocol (LSP) and the Debug Adapter Protocol
 * (DAP) frame their JSON payloads as:
 *
 *     Content-Length: <n>\r\n
 *     \r\n
 *     <n bytes of JSON>
 *
 * These helpers read/write such messages on a FILE stream (typically the
 * process' stdin/stdout).
 */
#ifndef ZAN_RPC_H
#define ZAN_RPC_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- transport-agnostic core ----
 *
 * The framing logic (Content-Length header parsing/formatting) is shared by
 * every transport. Callers supply byte-level read/write callbacks so the same
 * code drives a FILE stream, a TCP socket, or anything else.
 */

/* Read up to `n` bytes into `buf`; return the number of bytes read, or a
 * value <= 0 on EOF / error. */
typedef int (*rpc_reader_fn)(void *ctx, char *buf, int n);

/* Write exactly `n` bytes from `buf`; return true on success. */
typedef bool (*rpc_writer_fn)(void *ctx, const char *buf, int n);

/* Read one Content-Length framed message via `reader`. `max_len` caps the
 * accepted Content-Length (<= 0 means no limit). Returns a freshly malloc'd
 * NUL-terminated JSON body (caller frees), or NULL on EOF / malformed header /
 * oversize payload. */
char *rpc_read_message_cb(rpc_reader_fn reader, void *ctx, long max_len);

/* Write `payload` framed with a Content-Length header via `writer`.
 * Returns true on success. */
bool rpc_write_message_cb(rpc_writer_fn writer, void *ctx, const char *payload);

/* ---- FILE-stream convenience wrappers ---- */

/* Read one framed message from `in`. Returns a freshly malloc'd
 * NUL-terminated string containing the JSON payload (caller frees), or
 * NULL on EOF / malformed header. */
char *rpc_read_message(FILE *in);

/* Write `payload` (a NUL-terminated JSON string) to `out` with a proper
 * Content-Length header, then flush. */
void rpc_write_message(FILE *out, const char *payload);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_RPC_H */
