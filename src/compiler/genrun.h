/* genrun */
#ifndef ZAN_GENRUN_H
#define ZAN_GENRUN_H

#include <stddef.h>
#include <stdbool.h>

struct zan_ast_node;
struct zan_arena;
struct zan_diag;

/* Set by main() from --no-gen */
extern int zan_gen_enabled;

/* 内部辅助逻辑 */
#define ZAN_GEN_MAX_PATH 1024
int zan_gen_ensure(const char *stdlib_root, char *exe, size_t exe_size);

/* 内部辅助逻辑 */
int zan_gen_run(const char *exe, const char *meta_path, const char *out_path);

/* 核心系统底层抽象与内存语义契约 */
char **zan_gen_design(const char *stdlib_root, const char *const *paths,
                      size_t count);

/* 核心系统底层抽象与内存语义契约 */
bool zan_is_zcomp_path(const char *p);

/* 核心系统底层抽象与内存语义契约 */
bool zan_is_design_path(const char *p);

/* 内部辅助逻辑 */
int zan_gen_codegen(struct zan_ast_node *unit, struct zan_arena *arena,
                    struct zan_diag *diag, const char *stdlib_root);

/* 内部辅助逻辑 */
void zan_gen_take_source_texts(char ***texts, int *count);

/* 内部辅助逻辑 */
int zan_gen_cache_dir(char *dir, size_t dir_size);

#endif /* ZAN_GENRUN_H */
