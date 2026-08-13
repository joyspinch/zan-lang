/* genrun.c -- Zan-scripted code-generator runner (see genrun.h). */

#include "genrun.h"
#include "genmeta.h"
#include "arena.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "../common/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/host_oom.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

int zan_gen_enabled = 1;

#ifdef _WIN32
#define GEN_DIR_SEP_STR "\\"
#define GEN_EXE_SUFFIX ".exe"
#else
#define GEN_DIR_SEP_STR "/"
#define GEN_EXE_SUFFIX ""
#endif

/* The generator sources, relative to <stdlib_root>/System/Compiler/. A
 * fixed list (no directory scan): adding a source file means extending it. */
static const char *const kGenSources[] = {
    "ZanGen.zan", "GenCommon.zan", "GenForm.zan", "GenScene.zan",
    "GenJson.zan", "GenRoute.zan", "GenDb.zan", "GenDbEmit.zan"
};
#define GEN_SOURCE_COUNT ((int)(sizeof(kGenSources) / sizeof(kGenSources[0])))

/* Full path of the running zanc executable (needed to compile the generator
 * with ourselves). */
static void zan_self_exe(char *out, size_t outsz) {
#ifdef _WIN32
    GetModuleFileNameA(NULL, out, (DWORD)outsz);
#elif defined(__APPLE__)
    { uint32_t sz = (uint32_t)outsz;
      if (_NSGetExecutablePath(out, &sz) != 0) out[0] = '\0'; }
#else
    { ssize_t n = readlink("/proc/self/exe", out, outsz - 1);
      if (n > 0) out[n] = '\0'; else out[0] = '\0'; }
#endif
}

int zan_gen_cache_dir(char *dir, size_t dir_size) {
    const char *base;
#ifdef _WIN32
    base = getenv("LOCALAPPDATA");
    if (!base || !*base) return -1;
    if (snprintf(dir, dir_size, "%s\\Zan\\gen", base) <= 0) return -1;
    /* Create every missing level: CreateDirectory only makes the last one, so
     * a machine without %LOCALAPPDATA%\Zan yet got no cache directory at all
     * and the generator compile failed with "cannot emit object file". */
    for (char *p = dir + 1; *p; p++) {
        if (*p != '\\' && *p != '/') continue;
        char sep = *p;
        *p = '\0';
        CreateDirectoryA(dir, NULL); /* ok if it already exists */
        *p = sep;
    }
    if (!CreateDirectoryA(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return -1;
    }
    return 0;
#else
    base = getenv("XDG_CACHE_HOME");
    if (base && *base) {
        if (snprintf(dir, dir_size, "%s/zan/gen", base) <= 0) return -1;
    } else {
        base = getenv("HOME");
        if (!base || !*base) return -1;
        if (snprintf(dir, dir_size, "%s/.cache/zan/gen", base) <= 0) return -1;
    }
    /* Create every missing level: the cache root itself may not exist yet. */
    for (char *p = dir + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) { *p = '/'; return -1; }
        *p = '/';
    }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
#endif
}

/* Last-write time of a file; -1 when missing. (Values are only compared
 * within one platform, so the Windows/POSIX units need not match.) */
static long long zan_file_mtime(const char *path) {
#ifdef _WIN32
    HANDLE h = CreateFileA(path, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    FILETIME ft;
    GetFileTime(h, NULL, NULL, &ft);
    CloseHandle(h);
    return ((long long)ft.dwHighDateTime << 32) | (long long)ft.dwLowDateTime;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long long)st.st_mtime;
#endif
}

/* Spawn a child process and wait for it. argv[0] is the program. Returns the
 * exit code, or -1 when the child could not be spawned. The child inherits
 * our stdout/stderr, so its diagnostics pass through untouched. */
static int zan_spawn_wait(char *const argv[]) {
#ifdef _WIN32
    intptr_t r = _spawnv(_P_WAIT, argv[0], argv);
    if (r < 0) return -1;
    return (int)r;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execv(argv[0], argv);
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (!WIFEXITED(st)) return -1;
    return WEXITSTATUS(st);
#endif
}

int zan_gen_ensure(const char *stdlib_root, char *exe, size_t exe_size) {
    if (!zan_gen_enabled) return -1;
    char dir[ZAN_GEN_MAX_PATH];
    if (zan_gen_cache_dir(dir, sizeof(dir)) != 0) {
        fprintf(stderr, "error: no user cache dir for the code generators\n");
        return -1;
    }
    snprintf(exe, exe_size, "%s%sZanGen%s", dir, GEN_DIR_SEP_STR, GEN_EXE_SUFFIX);

    /* Cold cache, a generator source edited, or a relinked zanc: the metadata
     * protocol and the generator executable must match (the compiler binary
     * carries the exporter, the cached exe carries the consumers). */
    long long exe_mt = zan_file_mtime(exe);
    long long newest_input = -1;   /* newest generator input, for a re-check */
    int stale = (exe_mt < 0);
    {
        char zexe[ZAN_GEN_MAX_PATH];
        zan_self_exe(zexe, sizeof(zexe));
        long long self_mt = zexe[0] ? zan_file_mtime(zexe) : -1;
        if (self_mt > newest_input) newest_input = self_mt;
        if (!stale && self_mt > exe_mt) stale = 1;
    }
    for (int i = 0; i < GEN_SOURCE_COUNT; i++) {
        char src[ZAN_GEN_MAX_PATH];
        snprintf(src, sizeof(src), "%s%cSystem%cCompiler%c%s",
                 stdlib_root, GEN_DIR_SEP_STR[0], GEN_DIR_SEP_STR[0],
                 GEN_DIR_SEP_STR[0], kGenSources[i]);
        long long mt = zan_file_mtime(src);
        if (mt > newest_input) newest_input = mt;
        if (!stale && (mt < 0 || mt > exe_mt)) stale = 1;
    }

    if (stale) {
        char zexe[ZAN_GEN_MAX_PATH];
        char src[ZAN_GEN_MAX_PATH];
        zan_self_exe(zexe, sizeof(zexe));
        if (!zexe[0]) {
            fprintf(stderr, "error: cannot locate zanc to compile the code generators\n");
            return -1;
        }
        snprintf(src, sizeof(src), "%s%cSystem%cCompiler%cZanGen.zan",
                 stdlib_root, GEN_DIR_SEP_STR[0], GEN_DIR_SEP_STR[0],
                 GEN_DIR_SEP_STR[0]);
        fprintf(stderr, "zan: compiling code generators (first use; cached at %s)\n", exe);
        /* Compile to a per-process path and rename into place: parallel
         * builds share this cache, and a compile straight onto `exe` makes
         * them fight over the same intermediate object file. */
        char tmp[ZAN_GEN_MAX_PATH];
#ifdef _WIN32
        int pid = (int)_getpid();
#else
        int pid = (int)getpid();
#endif
        snprintf(tmp, sizeof(tmp), "%s%cZanGen_%d%s", dir,
                 GEN_DIR_SEP_STR[0], pid, GEN_EXE_SUFFIX);
        char *argv[] = {
            zexe, src, "--stdlib-path", (char *)stdlib_root, "--auto-stdlib",
            "--no-gen", "-DZAN_GEN_MAIN=1", "-o", tmp, NULL
        };
        int r = zan_spawn_wait(argv);
        if (r != 0) {
            remove(tmp);
            fprintf(stderr, "error: code-generator compile failed (exit %d)\n", r);
            return -1;
        }
#ifdef _WIN32
        if (!MoveFileExA(tmp, exe, MOVEFILE_REPLACE_EXISTING)) {
#else
        if (rename(tmp, exe) != 0) {
#endif
            /* A parallel build compiling the same generator may hold the
             * published image open (Windows cannot replace a running exe).
             * That is harmless as long as what it published is itself current,
             * so drop our copy and reuse theirs; a missing or stale
             * destination is a real failure. */
            remove(tmp);
            if (zan_file_mtime(exe) >= newest_input) return 0;
            fprintf(stderr, "error: cannot publish the compiled code generator\n");
            return -1;
        }
    }
    return 0;
}

int zan_gen_run(const char *exe, const char *meta_path, const char *out_path) {
    char *argv[] = { (char *)exe, (char *)meta_path, (char *)out_path, NULL };
    int r = zan_spawn_wait(argv);
    if (r != 0) {
        fprintf(stderr, "error: code generator failed (exit %d)\n", r);
        return -1;
    }
    return 0;
}

/* ---- design-document translation (the "design" mode) ---- */

static bool zan_is_design_path(const char *p) {
    size_t n = strlen(p);
    return (n > 6 && strcmp(p + n - 6, ".zform") == 0) ||
           (n > 7 && strcmp(p + n - 7, ".zscene") == 0);
}

static char *zan_read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf); fclose(f); return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    if (len_out) *len_out = (size_t)n;
    return buf;
}

char **zan_gen_design(const char *stdlib_root, const char *const *paths,
                      size_t count) {
    char **outs = (char **)calloc(count ? count : 1, sizeof(char *));
    if (!outs) {
        fprintf(stderr, "error: out of memory\n");
        return NULL;
    }

    /* Collect the design inputs; nothing to run without any. */
    int ndesign = 0;
    for (size_t i = 0; i < count; i++)
        if (zan_is_design_path(paths[i])) ndesign++;
    if (ndesign == 0) return outs;
    if (!zan_gen_enabled) {
        fprintf(stderr,
                "error: design document input needs the code generators, "
                "which --no-gen disables\n");
        free(outs);
        return NULL;
    }
    if (!stdlib_root || !stdlib_root[0]) {
        fprintf(stderr,
                "error: design document input needs the standard library "
                "to compile the code generators\n");
        free(outs);
        return NULL;
    }

    char dir[ZAN_GEN_MAX_PATH];
    if (zan_gen_cache_dir(dir, sizeof(dir)) != 0) {
        fprintf(stderr, "error: no user cache dir for the code generators\n");
        free(outs);
        return NULL;
    }
    char exe[ZAN_GEN_MAX_PATH];
    if (zan_gen_ensure(stdlib_root, exe, sizeof(exe)) != 0) {
        free(outs);
        return NULL;
    }

    /* Build the design request: { mode, files: [{ name, text, emitMain }] }.
     * `name` is the full input path; the generator echoes it back verbatim in
     * each reply source, which is how the texts are matched to inputs. */
    json_value *files = json_new_arr();
    for (size_t i = 0; i < count; i++) {
        if (!zan_is_design_path(paths[i])) continue;
        size_t tlen = 0;
        char *text = zan_read_file(paths[i], &tlen);
        if (!text) {
            fprintf(stderr, "error: cannot read '%s'\n", paths[i]);
            json_free(files);
            free(outs);
            return NULL;
        }
        /* A design document is JSON, and JSON must not start with a byte order
         * mark: an editor that saves the .zform as "UTF-8 with BOM" (Notepad,
         * PowerShell's Set-Content) would otherwise make the generator fail
         * with "cannot translate design document". Drop it. */
        const char *body = text;
        if (tlen >= 3 && (unsigned char)body[0] == 0xEF &&
            (unsigned char)body[1] == 0xBB && (unsigned char)body[2] == 0xBF)
            body += 3;
        json_value *f = json_new_obj();
        json_obj_set(f, "name", json_new_str(paths[i]));
        json_obj_set(f, "text", json_new_str(body));
        json_obj_set(f, "emitMain", json_new_bool(i == 0));
        json_arr_add(files, f);
        free(text);
    }
    json_value *req = json_new_obj();
    json_obj_set(req, "mode", json_new_str("design"));
    json_obj_set(req, "files", files);
    char *meta = json_serialize(req);
    json_free(req);

    char meta_path[ZAN_GEN_MAX_PATH], out_path[ZAN_GEN_MAX_PATH];
#ifdef _WIN32
    int pid = (int)_getpid();
#else
    int pid = (int)getpid();
#endif
    snprintf(meta_path, sizeof(meta_path), "%s%cgen_design_%d_in.json",
             dir, GEN_DIR_SEP_STR[0], pid);
    snprintf(out_path, sizeof(out_path), "%s%cgen_design_%d_out.json",
             dir, GEN_DIR_SEP_STR[0], pid);

    int ok = 0;
    FILE *mf = fopen(meta_path, "wb");
    if (!mf || !meta ||
        fwrite(meta, 1, strlen(meta), mf) != strlen(meta) ||
        fclose(mf) != 0) {
        fprintf(stderr, "error: cannot write generator request\n");
        if (mf) fclose(mf);
        ok = -1;
    }
    free(meta);
    if (ok == 0 && zan_gen_run(exe, meta_path, out_path) != 0) ok = -1;

    if (ok == 0) {
        size_t olen = 0;
        char *reply = zan_read_file(out_path, &olen);
        json_value *root = reply ? json_parse(reply) : NULL;
        if (!root) {
            fprintf(stderr, "error: invalid generator reply (design)\n");
            ok = -1;
        } else {
            json_value *err = json_obj_get(root, "error");
            if (err && err->type == JSON_STR && err->as.str && err->as.str[0]) {
                fprintf(stderr, "error: %s\n", err->as.str);
                ok = -1;
            } else {
                json_value *sources = json_obj_get(root, "sources");
                if (!sources || sources->type != JSON_ARR) {
                    fprintf(stderr, "error: generator reply has no sources\n");
                    ok = -1;
                } else {
                    for (int si = 0; si < sources->as.arr.count; si++) {
                        json_value *src = sources->as.arr.items[si];
                        const char *name = json_get_str(json_obj_get(src, "name"));
                        const char *text = json_get_str(json_obj_get(src, "text"));
                        if (!name || !text) continue;
                        for (size_t i = 0; i < count; i++) {
                            if (outs[i]) continue;
                            if (strcmp(paths[i], name) == 0) {
                                outs[i] = strdup(text);
                                break;
                            }
                        }
                    }
                }
            }
            json_free(root);
        }
        free(reply);
    }

    remove(meta_path);
    remove(out_path);
    if (ok != 0) {
        for (size_t i = 0; i < count; i++) free(outs[i]);
        free(outs);
        return NULL;
    }
    return outs;
}

/* ---- codegen mode (jsongen/dbgen/routegen) ---- */

/* Trigger filter: the generators run only when a call site could interest
 * them. The framework knowledge stays in Zan; this table is the compiler's
 * "which language shapes may trigger codegen" contract (like reserved words),
 * so a codebase with no Json/ORM/route calls never spawns a subprocess. */
static bool zan_trigger_json(const char *name) {
    return strcmp(name, "Deserialize") == 0 || strcmp(name, "Serialize") == 0;
}

/* json trigger over the exported metadata: any Json.Deserialize/Json.Serialize
 * call site (the calls array carries the bare method name in "name"). */
static bool zan_trigger_json_meta(const char *meta) {
    json_value *m = json_parse(meta);
    if (!m) return false;
    int hit = 0;
    json_value *calls = json_obj_get(m, "calls");
    if (calls && calls->type == JSON_ARR) {
        for (int i = 0; i < calls->as.arr.count && !hit; i++) {
            const char *name = json_get_str(
                json_obj_get(calls->as.arr.items[i], "name"));
            if (name && zan_trigger_json(name)) hit = 1;
        }
    }
    json_free(m);
    return hit;
}

/* routegen trigger: a controller-shaped class -- name suffix, base class, or
 * [Route]/[ApiController] attribute. (Framework knowledge stays in Zan; this
 * is only the compiler's spawn filter, like the reserved words.) */
static bool zan_trigger_route(const char *meta) {
    json_value *m = json_parse(meta);
    if (!m) return false;
    int hit = 0;
    json_value *classes = json_obj_get(m, "classes");
    if (classes && classes->type == JSON_ARR) {
        for (int i = 0; i < classes->as.arr.count && !hit; i++) {
            json_value *c = classes->as.arr.items[i];
            const char *name = json_get_str(json_obj_get(c, "name"));
            size_t nl = name ? strlen(name) : 0;
            if (name && nl >= 10 && strcmp(name + nl - 10, "Controller") == 0) {
                hit = 1;
                break;
            }
            json_value *bases = json_obj_get(c, "bases");
            if (bases && bases->type == JSON_ARR) {
                for (int j = 0; j < bases->as.arr.count && !hit; j++) {
                    const char *b = json_get_str(bases->as.arr.items[j]);
                    if (b && (strcmp(b, "Controller") == 0 ||
                              strcmp(b, "ApiController") == 0))
                        hit = 1;
                }
            }
            json_value *attrs = json_obj_get(c, "attrs");
            if (attrs && attrs->type == JSON_ARR) {
                for (int j = 0; j < attrs->as.arr.count && !hit; j++) {
                    const char *an = json_get_str(json_obj_get(
                        attrs->as.arr.items[j], "name"));
                    if (an && (strcmp(an, "Route") == 0 ||
                               strcmp(an, "ApiController") == 0))
                        hit = 1;
                }
            }
        }
    }
    json_free(m);
    return hit;
}

/* dbgen trigger: an ORM root call (Query/Select/Insert/Update/Delete/
 * SyncStructure*) anywhere, or a [Table]-attributed entity class (the
 * `<obj>.<Entity>.Where(...)` accessor sugar needs the class to exist). */
static bool zan_trigger_db_name(const char *name) {
    return strcmp(name, "Query") == 0 || strcmp(name, "Select") == 0 ||
           strcmp(name, "Insert") == 0 || strcmp(name, "Update") == 0 ||
           strcmp(name, "Delete") == 0 || strcmp(name, "SyncStructure") == 0 ||
           strcmp(name, "SyncStructureAsync") == 0 ||
           strcmp(name, "SyncStructureAll") == 0 ||
           strcmp(name, "SyncStructureAllAsync") == 0;
}

static bool zan_trigger_db_meta(const char *meta) {
    json_value *m = json_parse(meta);
    if (!m) return false;
    int hit = 0;
    json_value *calls = json_obj_get(m, "calls");
    if (calls && calls->type == JSON_ARR) {
        for (int i = 0; i < calls->as.arr.count && !hit; i++) {
            const char *name = json_get_str(
                json_obj_get(calls->as.arr.items[i], "name"));
            if (name && zan_trigger_db_name(name)) hit = 1;
        }
    }
    json_value *classes = json_obj_get(m, "classes");
    if (classes && classes->type == JSON_ARR) {
        for (int i = 0; i < classes->as.arr.count && !hit; i++) {
            json_value *attrs = json_obj_get(classes->as.arr.items[i], "attrs");
            if (attrs && attrs->type == JSON_ARR) {
                for (int j = 0; j < attrs->as.arr.count && !hit; j++) {
                    const char *an = json_get_str(json_obj_get(
                        attrs->as.arr.items[j], "name"));
                    if (an && strcmp(an, "Table") == 0) hit = 1;
                }
            }
        }
    }
    json_free(m);
    return hit;
}

static bool zan_gen_codegen_triggered(const char *meta) {
    return zan_trigger_json_meta(meta) || zan_trigger_route(meta) ||
           zan_trigger_db_meta(meta);
}

/* ---- rewrite directives ----
 *
 * The generators decide everything (framework names, SQL fragments, bind
 * lists, Expr<T> trees); the compiler is only the "surgeon": locate the
 * call site by id and mechanically rebuild the AST per the directive.
 *
 *   json_call   {id, callee}        retarget callee to __JsonBind.<callee>,
 *                                   drop type args
 *   db_root     {id, name, extra}   `recv.Xxx<T>(a)` -> `__DbBind.<name>(recv[, a])`
 *   db_acc_head {id, name, conn}    `<acc>.<Entity>` chain head ->
 *                                   `__DbBind.<name>(<conn>)` on the receiver
 *   db_acc_root {id, tree}          whole call replaced by <tree>
 *   db_chain    {id, ops:[{m,args}]} chain `<recv>.<m>(args)...`, replaces call
 *   db_expr_arg {id, param, tree}   args[param] replaced by <tree>
 *
 * Directives execute in descending id order, matching the C dbgen's
 * children-first visit: the chain root (`__DbBind.Q_T`) is rewritten before
 * the chain methods above it look for it. `tree` fields are expression trees
 * serialized by the exporter (genmeta.c) and rebuilt here verbatim. */

static zan_ast_node_t *rw_ident(zan_arena_t *arena, zan_loc_t loc,
                                const char *name) {
    zan_ast_node_t *n = zan_ast_new(arena, AST_IDENTIFIER, loc);
    size_t l = strlen(name);
    n->ident.name.str = zan_arena_strdup(arena, name, l);
    n->ident.name.len = (uint32_t)l;
    return n;
}

static zan_ast_node_t *rw_member(zan_arena_t *arena, zan_loc_t loc,
                                 zan_ast_node_t *obj, const char *name) {
    zan_ast_node_t *n = zan_ast_new(arena, AST_MEMBER_ACCESS, loc);
    n->member.object = obj;
    size_t l = strlen(name);
    n->member.name.str = zan_arena_strdup(arena, name, l);
    n->member.name.len = (uint32_t)l;
    n->member.null_cond = 0;
    return n;
}

static zan_ast_node_t *rw_call(zan_arena_t *arena, zan_loc_t loc,
                               zan_ast_node_t *callee, json_value *args) {
    zan_ast_node_t *n = zan_ast_new(arena, AST_CALL, loc);
    n->call.callee = callee;
    zan_ast_list_init(&n->call.args);
    zan_ast_list_init(&n->call.type_args);
    if (args && args->type == JSON_ARR) {
        for (int i = 0; i < args->as.arr.count; i++) {
            zan_ast_node_t *a =
                zan_genmeta_expr_from_json(args->as.arr.items[i], arena);
            if (a) zan_ast_list_push(&n->call.args, a, arena);
        }
    }
    return n;
}

static void rw_set_member_name(zan_arena_t *arena, zan_ast_node_t *ce,
                               const char *name) {
    size_t l = strlen(name);
    ce->member.name.str = zan_arena_strdup(arena, name, l);
    ce->member.name.len = (uint32_t)l;
}

static void rw_set_ident(zan_arena_t *arena, zan_ast_node_t *id,
                         const char *name) {
    size_t l = strlen(name);
    id->ident.name.str = zan_arena_strdup(arena, name, l);
    id->ident.name.len = (uint32_t)l;
}

/* Ascending-id order: call-site ids are assigned children-first (the same
 * order the old C dbgen visits), so ascending replays the exact rewrite sequence of
 * the C generators. The array is tiny, insertion sort is fine. */
static void rw_sort_desc(json_value *rw, int *order, int count) {
    for (int i = 1; i < count; i++) {
        int v = order[i];
        int j = i - 1;
        while (j >= 0 && json_get_num(
               json_obj_get(rw->as.arr.items[order[j]], "id"), 0) >
               json_get_num(json_obj_get(rw->as.arr.items[v], "id"), 0)) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
}

static void zan_apply_rewrites(zan_ast_node_t *unit, json_value *rw,
                               zan_arena_t *arena) {
    if (!rw || rw->type != JSON_ARR) return;
    int count = rw->as.arr.count;
    if (count == 0) return;
    int *order = (int *)malloc((size_t)count * sizeof(int));
    if (!order) return;
    for (int i = 0; i < count; i++) order[i] = i;
    rw_sort_desc(rw, order, count);

    /* Snapshot the call sites before rewriting anything: rewrites splice in
     * new call nodes (e.g. `__DbBind.Q_T(...)`), which would shift the
     * walking counter a fresh find_call relies on. Node *pointers* never
     * change, so the snapshot stays exact for the whole pass. */
    int total = zan_genmeta_index_calls(unit, NULL, 0);
    zan_ast_node_t **idx = NULL;
    if (total > 0) {
        idx = (zan_ast_node_t **)malloc((size_t)total * sizeof(zan_ast_node_t *));
        if (idx) zan_genmeta_index_calls(unit, idx, total);
    }

    for (int i = 0; i < count; i++) {
        json_value *op = rw->as.arr.items[order[i]];
        const char *opname = json_get_str(json_obj_get(op, "op"));
        if (!opname) continue;
        int id = (int)json_get_num(json_obj_get(op, "id"), 0);
        zan_ast_node_t *call =
            (idx && id >= 1 && id <= total) ? idx[id - 1] : NULL;
        if (!call || call->kind != AST_CALL) continue;
        zan_ast_node_t *ce = call->call.callee;
        zan_loc_t loc = call->loc;

        if (strcmp(opname, "json_call") == 0) {
            const char *callee = json_get_str(json_obj_get(op, "callee"));
            if (!callee || !ce || ce->kind != AST_MEMBER_ACCESS) continue;
            zan_ast_node_t *obj = ce->member.object;
            if (!obj || obj->kind != AST_IDENTIFIER) continue;
            rw_set_ident(arena, obj, "__JsonBind");
            rw_set_member_name(arena, ce, callee);
            call->call.type_args.count = 0;
            continue;
        }

        if (strcmp(opname, "db_root") == 0) {
            const char *name = json_get_str(json_obj_get(op, "name"));
            if (!name || !ce || ce->kind != AST_MEMBER_ACCESS) continue;
            zan_ast_node_t *recv = ce->member.object;
            json_value *extra = json_obj_get(op, "extra");
            bool keep_extra = json_get_bool(extra, false) &&
                              call->call.args.count > 0;
            zan_ast_node_t *ex =
                keep_extra ? call->call.args.items[0] : NULL;
            /* a fresh node replaces the callee object; `recv` (the original
             * receiver) must stay intact because it becomes the first
             * argument below */
            ce->member.object = rw_ident(arena, loc, "__DbBind");
            rw_set_member_name(arena, ce, name);
            call->call.type_args.count = 0;
            zan_ast_list_init(&call->call.args);
            zan_ast_list_push(&call->call.args, recv, arena);
            if (ex) zan_ast_list_push(&call->call.args, ex, arena);
            continue;
        }

        if (strcmp(opname, "db_acc_head") == 0) {
            const char *name = json_get_str(json_obj_get(op, "name"));
            json_value *conn = json_obj_get(op, "conn");
            if (!name || !ce || ce->kind != AST_MEMBER_ACCESS) continue;
            zan_ast_node_t *b = rw_ident(arena, loc, "__DbBind");
            zan_ast_node_t *m = rw_member(arena, loc, b, name);
            zan_ast_node_t *c = zan_ast_new(arena, AST_CALL, loc);
            c->call.callee = m;
            zan_ast_list_init(&c->call.args);
            zan_ast_list_init(&c->call.type_args);
            zan_ast_node_t *conn_n = conn
                ? zan_genmeta_expr_from_json(conn, arena) : NULL;
            if (conn_n) zan_ast_list_push(&c->call.args, conn_n, arena);
            ce->member.object = c;
            continue;
        }

        if (strcmp(opname, "db_acc_root") == 0) {
            zan_ast_node_t *repl = zan_genmeta_expr_from_json(
                json_obj_get(op, "tree"), arena);
            if (repl) *call = *repl;
            continue;
        }

        if (strcmp(opname, "db_chain") == 0) {
            json_value *ops = json_obj_get(op, "ops");
            if (!ops || ops->type != JSON_ARR || !ce ||
                ce->kind != AST_MEMBER_ACCESS)
                continue;
            zan_ast_node_t *node = ce->member.object;
            for (int k = 0; k < ops->as.arr.count; k++) {
                json_value *o = ops->as.arr.items[k];
                const char *m = json_get_str(json_obj_get(o, "m"));
                if (!m) continue;
                zan_ast_node_t *c = rw_call(
                    arena, loc, rw_member(arena, loc, node, m),
                    json_obj_get(o, "args"));
                node = c;
            }
            *call = *node;
            continue;
        }

        if (strcmp(opname, "db_expr_arg") == 0) {
            int param = (int)json_get_num(json_obj_get(op, "param"), -1);
            zan_ast_node_t *t = zan_genmeta_expr_from_json(
                json_obj_get(op, "tree"), arena);
            if (t && param >= 0 && param < call->call.args.count)
                call->call.args.items[param] = t;
            continue;
        }
    }
    free(idx);
    free(order);
}

/* Parse each generated source and merge its declarations into the unit. */
static void zan_merge_sources(zan_ast_node_t *unit, json_value *sources,
                              zan_arena_t *arena, zan_diag_t *diag) {
    if (!sources || sources->type != JSON_ARR) return;
    for (int i = 0; i < sources->as.arr.count; i++) {
        json_value *src = sources->as.arr.items[i];
        const char *text = json_get_str(json_obj_get(src, "text"));
        if (!text) continue;
        zan_lexer_t lex;
        zan_lexer_init(&lex, text, strlen(text), 0, arena, diag);
        zan_parser_t gp;
        zan_parser_init(&gp, &lex, arena, diag);
        zan_ast_node_t *gu = zan_parser_parse(&gp);
        if (!gu) continue;
        for (int k = 0; k < gu->comp_unit.decls.count; k++)
            zan_ast_list_push(&unit->comp_unit.decls, gu->comp_unit.decls.items[k],
                              arena);
    }
}

/* Report generator diagnostics (errors/warnings) through the compiler's
 * diag sink, where they carry the user file:line locations the generator
 * embedded in the metadata. */
static void zan_report_diags(zan_diag_t *diag, json_value *arr, bool is_error) {
    if (!arr || arr->type != JSON_ARR) return;
    for (int i = 0; i < arr->as.arr.count; i++) {
        json_value *d = arr->as.arr.items[i];
        int file = (int)json_get_num(json_obj_get(d, "file"), 0);
        int line = (int)json_get_num(json_obj_get(d, "line"), 0);
        int col = (int)json_get_num(json_obj_get(d, "col"), 0);
        const char *msg = json_get_str(json_obj_get(d, "msg"));
        if (!msg) continue;
        zan_diag_emit(diag, is_error ? DIAG_ERROR : DIAG_WARNING,
                      zan_loc((uint32_t)file, (uint32_t)line, (uint32_t)col, 0),
                      "%s", msg);
    }
}

/* Run the Zan-scripted code generators over the compilation unit: export the
 * metadata, run the cached generator executable, then apply the reply --
 * generated sources are parsed and merged, rewrite directives retarget call
 * sites, diagnostics flow into `diag`. Returns 0 on success (or when nothing
 * triggered), -1 on failure with a message on stderr. */
int zan_gen_codegen(zan_ast_node_t *unit, zan_arena_t *arena,
                    zan_diag_t *diag, const char *stdlib_root) {
    if (!zan_gen_enabled || !unit) return 0;

    char *meta = zan_genmeta_export(unit);
    if (!meta) return 0;
    int triggered = zan_gen_codegen_triggered(meta);
    if (!triggered) {
        free(meta);
        return 0;
    }
    if (!stdlib_root || !stdlib_root[0]) {
        fprintf(stderr, "error: code generation needs the standard library\n");
        free(meta);
        return -1;
    }

    char dir[ZAN_GEN_MAX_PATH];
    if (zan_gen_cache_dir(dir, sizeof(dir)) != 0) {
        fprintf(stderr, "error: no user cache dir for the code generators\n");
        free(meta);
        return -1;
    }
    char exe[ZAN_GEN_MAX_PATH];
    if (zan_gen_ensure(stdlib_root, exe, sizeof(exe)) != 0) {
        free(meta);
        return -1;
    }

    char meta_path[ZAN_GEN_MAX_PATH], out_path[ZAN_GEN_MAX_PATH];
#ifdef _WIN32
    int pid = (int)_getpid();
#else
    int pid = (int)getpid();
#endif
    snprintf(meta_path, sizeof(meta_path), "%s%cgen_codegen_%d_in.json",
             dir, GEN_DIR_SEP_STR[0], pid);
    snprintf(out_path, sizeof(out_path), "%s%cgen_codegen_%d_out.json",
             dir, GEN_DIR_SEP_STR[0], pid);

    int rc = -1;
    /* Wrap the metadata in the codegen request: the generator reads
     * { mode, unit }. `meta` is a JSON object, so a textual splice keeps it
     * verbatim (no double serialization). */
    size_t req_len = strlen(meta) + 64;
    char *req_text = (char *)malloc(req_len);
    if (!req_text) {
        fprintf(stderr, "error: out of memory\n");
        free(meta);
        remove(meta_path);
        return -1;
    }
    snprintf(req_text, req_len, "{\"mode\":\"codegen\",\"unit\":%s}", meta);
    free(meta);
    FILE *mf = fopen(meta_path, "wb");
    if (!mf || fwrite(req_text, 1, strlen(req_text), mf) != strlen(req_text) ||
        fclose(mf) != 0) {
        fprintf(stderr, "error: cannot write generator request\n");
        if (mf) fclose(mf);
        free(req_text);
        remove(meta_path);
        return -1;
    }
    free(req_text);

    if (zan_gen_run(exe, meta_path, out_path) != 0) goto done;

    {
        size_t olen = 0;
        char *reply = zan_read_file(out_path, &olen);
        json_value *root = reply ? json_parse(reply) : NULL;
        if (!root) {
            fprintf(stderr, "error: invalid generator reply (codegen)\n");
            free(reply);
            goto done;
        }
        const char *err = json_get_str(json_obj_get(root, "error"));
        if (err && err[0]) {
            fprintf(stderr, "error: %s\n", err);
            json_free(root);
            free(reply);
            goto done;
        }
        json_value *errors = json_obj_get(root, "errors");
        json_value *warnings = json_obj_get(root, "warnings");
        zan_report_diags(diag, warnings, false);
        zan_report_diags(diag, errors, true);
        /* Rewrites were already decided while scanning (matching the old
         * in-place C generators); generated sources are only merged when no
         * generator diagnostics were raised. */
        zan_apply_rewrites(unit, json_obj_get(root, "rewrites"), arena);
        if (errors && errors->type == JSON_ARR && errors->as.arr.count > 0) {
            json_free(root);
            free(reply);
            rc = 0; /* diagnostics already reported; main() fails later */
            goto done;
        }
        zan_merge_sources(unit, json_obj_get(root, "sources"), arena, diag);
        /* ZAN_GEN_REPLY=<path>: keep a copy of the reply (test hook --
         * tests/gen/ asserts on the generated text and rewrite directives). */
        {
            const char *gr = getenv("ZAN_GEN_REPLY");
            if (gr && gr[0]) {
                FILE *cf = fopen(gr, "wb");
                if (cf) {
                    FILE *rf = fopen(out_path, "rb");
                    if (rf) {
                        char buf[8192];
                        size_t n;
                        while ((n = fread(buf, 1, sizeof(buf), rf)) > 0)
                            fwrite(buf, 1, n, cf);
                        fclose(rf);
                    }
                    fclose(cf);
                }
            }
        }
        json_free(root);
        free(reply);
        rc = 0;
    }

done:
    remove(meta_path);
    remove(out_path);
    return rc;
}

