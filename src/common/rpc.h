/* rpc */
#ifndef ZAN_RPC_H
#define ZAN_RPC_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 内部辅助逻辑 */

/* 内部辅助逻辑 */
typedef int (*rpc_reader_fn)(void *ctx, char *buf, int n);

/* Write exactly `n` bytes from `buf`; return true on success. */
typedef bool (*rpc_writer_fn)(void *ctx, const char *buf, int n);

/* Read one Content-Length framed message via `reader` */
char *rpc_read_message_cb(rpc_reader_fn reader, void *ctx, long max_len);

/* Default size cap applied by rpc_read_message (the FILE wrapper): 64 MB */
#define RPC_MAX_MESSAGE (64L * 1024 * 1024)

/* Write `payload` framed with a Content-Length header via `writer` */
bool rpc_write_message_cb(rpc_writer_fn writer, void *ctx, const char *payload);

/* ---- FILE-stream convenience wrappers ---- */

/* Read one framed message from `in` */
char *rpc_read_message(FILE *in);

/* 内部辅助逻辑 */
void rpc_write_message(FILE *out, const char *payload);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_RPC_H */
