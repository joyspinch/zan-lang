/* 底层系统交互与数据协议契约 */

#ifndef ZAN_DIAG_H
#define ZAN_DIAG_H

#include "zan.h"

typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_NOTE,
} zan_diag_level_t;

/* 单行最大打印错误数，超过则折叠以抑制级联报错噪音 */
#define ZAN_DIAG_MAX_ERRORS_PER_LINE 8

/* 诊断回显源码行最大字符宽度，超长行截断居中展示 */
#define ZAN_DIAG_MAX_SOURCE_ECHO 200

/* 底层系统交互与数据协议契约 */
typedef struct {
    zan_diag_level_t level;
    zan_loc_t        loc;
    char             message[512];
} zan_diag_entry_t;

struct zan_diag {
    int error_count;
    int warning_count;
    int max_errors;
    /* 达到 max_errors 错误上限后丢弃的错误数 */
    int  suppressed_errors;
    bool limit_notice_shown;
    const char *const *file_names;  /* indexed by file_id */
    const char *const *file_sources; /* indexed by file_id */
    int file_count;

    /* 结构化诊断捕获（供 LSP 服务等工具消费） */
    bool              capture;
    bool              treat_warnings_as_errors;
    zan_diag_entry_t *entries;
    int               entry_count;
    int               entry_cap;

    /* 级联报错抑制标记 */
    uint32_t dup_file_id;
    uint32_t dup_line;
    int      dup_line_errors;
    int      dup_line_suppressed;
    bool     dup_line_notice_shown;
};

zan_diag_t *zan_diag_new(zan_arena_t *arena);
void zan_diag_set_deny_warnings(zan_diag_t *diag, bool enabled);

/* 诊断信息输出条数上限 */
void zan_diag_set_max_errors(zan_diag_t *diag, int max_errors);

/* 因超出错误条数上限而丢弃的错误计数 */
int zan_diag_suppressed_errors(const zan_diag_t *diag);
void zan_diag_add_file(zan_diag_t *diag, const char *name, const char *source);
void zan_diag_emit(zan_diag_t *diag, zan_diag_level_t level, zan_loc_t loc,
                   const char *fmt, ...);
bool zan_diag_has_errors(zan_diag_t *diag);

/* 开启或关闭结构化诊断捕获（开启后诊断不打印至 stderr 而是存入内存数组） */
void zan_diag_set_capture(zan_diag_t *diag, bool enabled);

/* 核心系统底层抽象与内存语义契约 */
int  zan_diag_entry_count(const zan_diag_t *diag);
const zan_diag_entry_t *zan_diag_entry_at(const zan_diag_t *diag, int index);

/* 释放堆分配的结构化诊断捕获缓冲区及文件记录数组 */
void zan_diag_free_buffers(zan_diag_t *diag);

/* 编译进度追踪打印（仅在设置 ZANC_TRACE 环境变量时输出） */
void zan_compile_trace(const char *fmt, ...);

#endif /* ZAN_DIAG_H */
