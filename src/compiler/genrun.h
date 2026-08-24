/* genrun.h -- run the Zan-scripted code generators (ZanGen).
 *
 * The compiler's source-generators (formgen/scenegen/jsongen/routegen/dbgen)
 * were moved out of C into Zan code living in stdlib/System/Compiler/. zanc
 * drives them through a subprocess: it exports the compilation-unit metadata
 * to a JSON file, runs the cached generator executable over it, and consumes
 * the generated source + rewrite directives from the reply JSON.
 *
 * The generator is compiled with zanc itself (it is ordinary Zan code) and
 * cached under the user cache dir, keyed by the compiler, stdlib root and
 * generator source contents: a cold cache costs one extra compile, a warm
 * cache one spawn per build.
 *
 * --no-gen (zan_gen_enabled = 0) disables the whole machinery; it exists to
 * compile the generators themselves, whose sources must not run the codegen
 * passes on themselves (a bootstrap loop).
 */
#ifndef ZAN_GENRUN_H
#define ZAN_GENRUN_H

#include <stddef.h>

struct zan_ast_node;
struct zan_arena;
struct zan_diag;

/* Set by main() from --no-gen. Generators run only while this is non-zero. */
extern int zan_gen_enabled;

/* Ensure the content-addressed generator executable exists for this compiler
 * and <stdlib_root>. Compiles it with zanc itself when cold. Fills `exe`
 * (caller's buffer, at least ZAN_GEN_MAX_PATH bytes) on success. Returns 0,
 * or -1 with a message on stderr. */
#define ZAN_GEN_MAX_PATH 1024
int zan_gen_ensure(const char *stdlib_root, char *exe, size_t exe_size);

/* Run the generator over the metadata file `meta_path`, writing its reply
 * JSON to `out_path`. The generator's stderr passes through (its diagnostics
 * carry the user file:line locations embedded in the metadata). Returns 0 on
 * success, -1 on failure (message on stderr). */
int zan_gen_run(const char *exe, const char *meta_path, const char *out_path);

/* Translate every .zform/.zscene design input in one generator run (the
 * "design" mode request) and return a malloc'd array of `count` translated
 * source texts; entries for non-design files are NULL and ownership of every
 * entry passes to the caller (one free() each, plus free() on the array).
 * Returns NULL on failure with a diagnostic already printed on stderr.
 * `stdlib_root` is needed only when a design file is present. */
char **zan_gen_design(const char *stdlib_root, const char *const *paths,
                      size_t count);

/* Run the Zan-scripted code generators (the "codegen" mode: jsongen/dbgen/
 * routegen) over the compilation unit. Exports the metadata, spawns the
 * cached generator when a call site could trigger codegen, then applies the
 * reply: generated sources are parsed and merged into the unit, rewrite
 * directives retarget call sites, diagnostics flow into `diag`. Returns 0 on
 * success (or when nothing triggered), -1 on failure (message on stderr). */
int zan_gen_codegen(struct zan_ast_node *unit, struct zan_arena *arena,
                    struct zan_diag *diag, const char *stdlib_root);

/* Fill `dir` with the generator cache directory (creating it), or return -1
 * when no user cache dir can be located. */
int zan_gen_cache_dir(char *dir, size_t dir_size);

#endif /* ZAN_GENRUN_H */
