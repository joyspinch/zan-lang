
#ifndef ZAN_CHECKER_H
#define ZAN_CHECKER_H

#include "zan.h"
#include "ast.h"
#include "binder.h"

struct checker_local;

struct zan_checker {
    zan_binder_t *binder;
    zan_arena_t *arena;
    zan_diag_t *diag;
    zan_type_t *current_return_type;
    bool in_no_runtime;
    zan_symbol_t *current_type_sym;
    bool in_ctor;
    struct checker_local *locals;
    zan_ast_node_t *last_call_node;
    zan_symbol_t *last_call_method;
    /* [Table] 实体名清单：SyncStructureAll(Async) 零参调用点合成
     * Meta[] 实参用；zan_checker_check 入口对编译单元预扫描收集。 */
    zan_istr_t *table_ents;
    int table_ent_count;
    int table_ent_cap;
    bool table_ents_done;
    /* Controller 类名清单：__AttrRoutes.Register 调用点合成 TypeInfo[] 实参用 */
    zan_istr_t *ctrl_ents;
    int ctrl_ent_count;
    int ctrl_ent_cap;
    bool ctrl_ents_done;
    /* 用户类名清单：DocApp.Bootstrap() 零参调用点合成 VmFactory[] 实参
     * 用（B-ID147 框架化窗口引导，逐类真 new 交给框架按名挑 code-behind）。 */
    zan_istr_t *vm_ents;
    int vm_ent_count;
    int vm_ent_cap;
    bool vm_ents_done;
    zan_ast_node_t *current_body;
    zan_ast_node_t *current_method_decl;
    /* 已经报过"null 绑不上值类型形参"的实参位置：同一个实参只报一次
     * （唯一方法路径与重载路径会撞上同一处）。 */
    zan_loc_t null_arg_reported;
    bool has_null_arg_reported;
    /* 正在检查调用实参（B-ID65）：显式 (ExprNode/Expr<T>) 型 lambda 形参
     * 的体在此刻不查——树 lambda 在 irgen 发射口降级为 ExprNode 构造树，
     * 体里的 x.F 在此查会误报 ExprNode 上无成员（E2084 假阳性）。
     * 非调用位置照旧查体。 */
    int in_call_args;
};

void zan_checker_init(zan_checker_t *c, zan_binder_t *binder,
                      zan_arena_t *arena, zan_diag_t *diag);
void zan_checker_check(zan_checker_t *c, zan_ast_node_t *unit);

zan_type_t *zan_checker_check_expr(zan_checker_t *c, zan_ast_node_t *expr);
void zan_checker_check_stmt(zan_checker_t *c, zan_ast_node_t *stmt);

#endif /* ZAN_CHECKER_H */
