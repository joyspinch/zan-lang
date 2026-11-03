
#ifndef ZAN_LEXER_H
#define ZAN_LEXER_H

#include "zan.h"
#include "token.h"

#define ZAN_PP_MAX_DEFINES 2048
#define ZAN_PP_MAX_COND_DEPTH 64

typedef struct {
    char name[64];
    char value[256];
} zan_pp_define_t;

struct zan_token {
    zan_token_kind_t kind;
    zan_loc_t loc;
    /* 整数字面量类型后缀编码 (0=无, 1=L, 2=U, 3=UL) */
    int lit_suffix;
    /* 整数字面量进制 (2/8/10/16) */
    unsigned char lit_radix;
    union {
        int64_t int_val;
        double float_val;
        zan_istr_t str_val;
    };
};

#define ZAN_MAX_INTERP_DEPTH 512

/* 字符串插值挖洞内的括号嵌套深度，用于区分闭合大括号 */
typedef struct {
    int brace;
    int paren;
    int bracket;
    bool verbatim;
    bool raw;   /* $""" 插值原始串：恢复段走 raw 规则 */
    int strip;  /* 多行闭合行缩进剥离列数 */
} zan_interp_level_t;

struct zan_lexer {
    const char *source;
    size_t source_len;
    size_t pos;
    uint32_t line;
    uint32_t col;
    uint32_t file_id;
    zan_arena_t *arena;
    zan_diag_t *diag;
    /* 当前打开的嵌套字符串插值洞栈 */
    int interp_depth;
    zan_interp_level_t interp_stack[ZAN_MAX_INTERP_DEPTH];

    /* 词法分析器 arena 堆分配状态（支持语法分析器回溯快照） */
    zan_pp_define_t *defines;
    int define_count;
    int define_cap;
    int cond_stack[ZAN_PP_MAX_COND_DEPTH];
    int cond_depth;
    int cond_seen_true[ZAN_PP_MAX_COND_DEPTH];
    /* 条件编译预处理嵌套深度溢出计数 */
    int cond_overflow;
};

void zan_lexer_init(zan_lexer_t *lex, const char *source, size_t len,
                    uint32_t file_id, zan_arena_t *arena, zan_diag_t *diag);
zan_token_t zan_lexer_next(zan_lexer_t *lex);
zan_token_t zan_lexer_peek(zan_lexer_t *lex);
/* 向前预看两个词法单元（不消费 token，完全恢复词法状态） */
zan_token_t zan_lexer_peek2(zan_lexer_t *lex);
/* 向前预看 n 个词法单元（n >= 1，不消费 token，完全恢复词法状态） */
zan_token_t zan_lexer_peek_n(zan_lexer_t *lex, int n);

/* 回溯快照：只存位置态与深度计数，替代整结构体拷贝
 * （zan_lexer_t 含 512 层 interp 栈 ~10KB，parser lookahead 消歧热路径拷不起）。
 * interp 洞栈只回存 mark 深度以内的内容（深度 0 时不分配）；
 * defines/cond 栈按计数截断恢复（投机扫描中途的 #define/#if 尾巴无害）。 */
typedef struct {
    size_t pos;
    uint32_t line;
    uint32_t col;
    int interp_depth;
    zan_interp_level_t *interp_tail; /* arena 拷贝；深度 0 为 NULL */
    int define_count;
    int cond_depth;
    int cond_overflow;
} zan_lexer_mark_t;

void zan_lexer_mark(zan_lexer_t *lex, zan_lexer_mark_t *out, zan_arena_t *arena);
void zan_lexer_reset(zan_lexer_t *lex, const zan_lexer_mark_t *mark);

void zan_lexer_define(zan_lexer_t *lex, const char *name, const char *value);

/* 关键字 token → 词面文本；不是关键字返回 NULL */
const char *zan_lexer_keyword_text(zan_token_kind_t kind);

#endif /* ZAN_LEXER_H */
