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

/* 底层系统交互与数据协议契约 */
typedef bool (*rpc_writer_fn)(void *ctx, const char *buf, int n);

/* 底层系统交互与数据协议契约 */
char *rpc_read_message_cb(rpc_reader_fn reader, void *ctx, long max_len);

/* 底层系统交互与数据协议契约 */
#define RPC_MAX_MESSAGE (64L * 1024 * 1024)

/* 底层系统交互与数据协议契约 */
bool rpc_write_message_cb(rpc_writer_fn writer, void *ctx, const char *payload);

/* 核心系统底层抽象与内存语义契约 */

/* 核心系统底层抽象与内存语义契约 */
char *rpc_read_message(FILE *in);

/* 内部辅助逻辑 */
void rpc_write_message(FILE *out, const char *payload);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_RPC_H */
