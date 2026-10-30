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

#define ZAN_GEN_MAX_PATH 1024
int zan_gen_ensure(const char *stdlib_root, char *exe, size_t exe_size);

int zan_gen_run(const char *exe, const char *meta_path, const char *out_path);

char **zan_gen_design(const char *stdlib_root, const char *const *paths,
                      size_t count);

bool zan_is_zcomp_path(const char *p);

bool zan_is_design_path(const char *p);



int zan_gen_cache_dir(char *dir, size_t dir_size);

#endif /* ZAN_GENRUN_H */
