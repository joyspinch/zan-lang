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

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

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
