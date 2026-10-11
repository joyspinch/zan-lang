/* rpc */
#ifndef ZAN_RPC_H
#define ZAN_RPC_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef int (*rpc_reader_fn)(void *ctx, char *buf, int n);

typedef bool (*rpc_writer_fn)(void *ctx, const char *buf, int n);

char *rpc_read_message_cb(rpc_reader_fn reader, void *ctx, long max_len);

#define RPC_MAX_MESSAGE (64L * 1024 * 1024)

bool rpc_write_message_cb(rpc_writer_fn writer, void *ctx, const char *payload);


char *rpc_read_message(FILE *in);

void rpc_write_message(FILE *out, const char *payload);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_RPC_H */
