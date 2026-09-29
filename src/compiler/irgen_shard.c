/* ------------------------------------------------------------------------- *
 * Opt-in object sharding (stage 4): ZAN_SHARD=1 splits eligible function
 * bodies into separate object files so the coordinator module the optimizer
 * and LLVMTargetMachineEmitToFile see shrinks by the moved share. Included
 * textually at the end of irgen.c (like irgen_emit.c / irgen_manifest.c).
 *
 * Cloning strategy — text round-trip, not handle surgery. Cloning
 * instructions into a fresh LLVMContext is not expressible with the LLVM C
 * API this toolchain ships (no LLVMGetGEPSourceElementType for BuildGEP2,
 * no LLVMCloneModule, no comdat reader, no instruction removal), but the
 * assembly printer already knows everything: each shard is assembled as .ll
 * text — member bodies printed verbatim, synthesized `declare` lines for
 * referenced functions, verbatim definitions for private constants that
 * travel, synthesized `external` declarations for globals that stay — and
 * parsed into a fresh context with LLVMParseIRInContext. Named struct types
 * are harvested by scanning the assembled text for %identifiers and
 * resolving them against the coordinator's type table (the GEP source type,
 * unreadable through the C API, only ever appears in printed text).
 *
 * Safety net: any parse/verify/emit failure, and every referenced symbol the
 * rules below cannot prove safe, falls back cleanly — linkages and
 * demotions are restored, temporary objects are deleted, and the compile
 * continues with the untouched single module. The coordinator's own bodies
 * are only deleted after every shard object has been emitted AND the
 * post-move module verifies.
 * ------------------------------------------------------------------------- */

#include <llvm-c/IRReader.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
static void sh_probe_mem(const char *tag) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        fprintf(stderr, "    [probe %-22s Commit: %4zu MB, Peak: %4zu MB]\n",
                tag, pmc.PagefileUsage / (1024 * 1024),
                pmc.PeakPagefileUsage / (1024 * 1024));
    }
#endif
}

enum {
    SH_F_DECL = 0,  /* referenced fn: safe to declare external */
    SH_F_DEMOTE,    /* internal __zan_* helper: demote to external + declare */
    SH_F_EXT,       /* internal user fn: stays, exports on reference + declare */
    SH_F_BLOCK      /* unusable reference (alias etc.): dirty */
};

enum {
    SH_G_DECL = 0,  /* referenced global: synthesized external declaration */
    SH_G_TRAVEL,    /* private/internal constant: definition duplicated into
                     * every shard whose body references it */
    SH_G_BLOCK      /* travel impossible (initializer escapes an internal
                     * fn / alias / TLS state): referers stay in coordinator */
};

/* ---- open-addressing pointer -> int map --------------------------------- */
typedef struct {
    void     **keys;
    int       *vals;      /* val >= 0 stored, -1 = empty slot */
    int        mask;
    int        n;
} sh_map_t;

static size_t sh_hash_ptr(void *v) {
    size_t x = (size_t)(uintptr_t)v;
    x ^= x >> 17; x *= (size_t)0x9E3779B9U;
    return x;
}

static void sh_map_init(sh_map_t *m) {
    m->mask = 1023;
    m->n = 0;
    m->keys = (void **)malloc(1024 * sizeof(*m->keys));
    m->vals = (int *)malloc(1024 * sizeof(*m->vals));
    for (int i = 0; i < 1024; i++) m->vals[i] = -1;
}

static void sh_map_free(sh_map_t *m) { free(m->keys); free(m->vals); }

static int sh_map_get(sh_map_t *m, void *k) {
    size_t i = sh_hash_ptr(k) & (size_t)m->mask;
    while (m->vals[i] >= 0) {
        if (m->keys[i] == k) return m->vals[i];
        i = (i + 1) & (size_t)m->mask;
    }
    return -1;
}

static bool sh_map_put(sh_map_t *m, void *k, int v) {
    if (m->n * 2 >= m->mask + 1) {
        int ncap = (m->mask + 1) * 4, nmask = ncap - 1;
        void **nk = (void **)malloc((size_t)ncap *
                                                  sizeof(*nk));
        int *nv = (int *)malloc((size_t)ncap * sizeof(*nv));
        if (!nk || !nv) { free(nk); free(nv); return false; }
        for (int i = 0; i < ncap; i++) nv[i] = -1;
        for (int i = 0; i <= m->mask; i++) {
            if (m->vals[i] < 0) continue;
            size_t j = sh_hash_ptr(m->keys[i]) & (size_t)nmask;
            while (nv[j] >= 0) j = (j + 1) & (size_t)nmask;
            nk[j] = m->keys[i]; nv[j] = m->vals[i];
        }
        free(m->keys); free(m->vals);
        m->keys = nk; m->vals = nv; m->mask = nmask;
    }
    size_t i = sh_hash_ptr(k) & (size_t)m->mask;
    while (m->vals[i] >= 0) {
        if (m->keys[i] == k) { m->vals[i] = v; return true; }
        i = (i + 1) & (size_t)m->mask;
    }
    m->keys[i] = k; m->vals[i] = v; m->n++;
    return true;
}

/* ---- growable string buffer --------------------------------------------- */
typedef struct { char *p; size_t n, cap; bool oom; } sh_sbuf_t;

static void sh_sb_putn(sh_sbuf_t *b, const char *s, size_t n) {
    if (b->oom || !n) return;
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->n + n + 1) nc *= 2;
        char *np = (char *)realloc(b->p, nc);
        if (!np) { b->oom = true; return; }
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
}

static void sh_sb_puts(sh_sbuf_t *b, const char *s) { sh_sb_putn(b, s, strlen(s)); }

/* ---- shard state --------------------------------------------------------- */
typedef struct { LLVMValueRef fn; LLVMValueRef glob; unsigned old_linkage; } sh_link_rec_t;
typedef struct { LLVMValueRef orig, decl; char *origname; } sh_move_rec_t;

typedef struct {
    zan_irgen_t      *g;
    sh_map_t          fn_v;        /* fn      -> SH_F_*  */
    sh_map_t          glob_v;      /* global  -> SH_G_*  */
    sh_map_t          members_all; /* every fn planned for any shard -> 1 */
    sh_map_t          needs_decl;  /* fn needs an external decl in coord */
    sh_map_t          type_done;   /* named type emitted in CURRENT shard */
    sh_link_rec_t    *link_recs;   int link_n, link_cap;
    sh_map_t          ext_globs;   /* DECL globals referenced from shards */
    sh_map_t          ext_fns;     /* internal fns exported on shard reference */
    bool              failed;
    char              reason[256];
} sh_state_t;

static void sh_fail(sh_state_t *st, const char *fmt, ...) {
    if (st->failed) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(st->reason, sizeof(st->reason), fmt, ap);
    va_end(ap);
    st->failed = true;
}

static bool sh_name_is(const LLVMValueRef v, const char *pfx) {
    const char *n = LLVMGetValueName(v);
    return n && strncmp(n, pfx, strlen(pfx)) == 0;
}

/* ---- function verdicts ---------------------------------------------------- */
/* A coordinator body referenced from a shard must be reachable through an
 * external symbol. Declarations already are; defined bodies exported by the
 * coordinator are; internal helpers with the compiler-owned __zan_ prefix
 * can safely be demoted to external (no libc collision is possible); any
 * other internal body (user fns made module-local, generic specs) stays in
 * the coordinator and is exported when a shard references it — mangled Zan
 * names are unique, so the linkage flip is a pure visibility change. */
static int sh_fn_verdict(sh_state_t *st, LLVMValueRef f) {
    int hit = sh_map_get(&st->fn_v, f);
    if (hit >= 0) return hit;
    int v = SH_F_DECL;
    if (!LLVMIsDeclaration(f)) {
        unsigned lk = LLVMGetLinkage(f);
        if (lk == LLVMInternalLinkage || lk == LLVMPrivateLinkage)
            v = sh_name_is(f, "__zan_") ? SH_F_DEMOTE : SH_F_EXT;
    }
    sh_map_put(&st->fn_v, f, v);
    return v;
}

/* True when the function or any instruction in it carries attached metadata.
 * The fragment printer cannot emit metadata definitions, so a moved body with
 * attachments would parse as "use of undefined metadata !N" — the -O2
 * pipeline attaches !llvm.loop / !llvm.access.group to vectorized loops. Such
 * bodies must stay coordinator-side. */
static bool sh_has_attached_metadata(LLVMValueRef f) {
    if (LLVMHasMetadata(f)) return true;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(f); bb;
         bb = LLVMGetNextBasicBlock(bb))
        for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
             in = LLVMGetNextInstruction(in))
            if (LLVMHasMetadata(in)) return true;
    return false;
}

/* ---- global verdicts (with initializer closure) --------------------------- */
/* A global travels only when it is a private/internal constant whose whole
 * initializer closure can be reconstructed inside the shard: function
 * references must be declarable (SH_F_BLOCK poisons), nested constants must
 * resolve, aliases and TLS state poison. Cyclic initializers (constant A
 * referencing constant B referencing A) poison both members: the -2
 * in-progress memo makes the re-entrant query conservative. */
typedef struct { LLVMValueRef v; int depth; } sh_cw_item_t;

static int sh_global_verdict(sh_state_t *st, LLVMValueRef gv);

/* Recursive initializer walk; returns false when the closure poisons. */
static bool sh_glob_closure_ok(sh_state_t *st, LLVMValueRef init,
                               sh_map_t *done, int depth) {
    if (!init || depth > 32) return true;
    if (sh_map_get(done, init) >= 0) return true;
    if (!sh_map_put(done, init, 1)) { sh_fail(st, "out of memory"); return false; }
    if (LLVMIsAFunction(init))
        return sh_fn_verdict(st, init) != SH_F_BLOCK;
    if (LLVMIsAGlobalAlias(init)) return false;
    if (LLVMIsAGlobalVariable(init)) {
        int v = sh_global_verdict(st, init);
        if (v == SH_G_BLOCK) return false;
        if (v == SH_G_TRAVEL && !LLVMIsDeclaration(init))
            return sh_glob_closure_ok(st, LLVMGetInitializer(init), done,
                                      depth + 1);
        return true;
    }
    if (LLVMIsAConstantExpr(init)) {
        unsigned nop = LLVMGetNumOperands(init);
        for (unsigned i = 0; i < nop; i++)
            if (!sh_glob_closure_ok(st, LLVMGetOperand(init, (int)i), done,
                                    depth + 1))
                return false;
    }
    return true;
}

static int sh_global_verdict(sh_state_t *st, LLVMValueRef gv) {
    int hit = sh_map_get(&st->glob_v, gv);
    if (hit >= 0) return hit;
    if (hit == -2) return SH_G_BLOCK;   /* cyclic initializer */

    int verdict;
    if (!LLVMIsAGlobalVariable(gv)) {
        verdict = SH_G_BLOCK;           /* alias: cannot declare safely */
    } else if (LLVMHasMetadata(gv)) {
        /* attached metadata nodes print as bare !N references; the fragment
         * carries no metadata definitions, so the parsed shard would reject
         * them ("use of undefined metadata"). Stay coordinator-side. */
        verdict = SH_G_BLOCK;
    } else if (LLVMIsDeclaration(gv)) {
        verdict = SH_G_DECL;
    } else if (LLVMIsThreadLocal(gv)) {
        /* mutable per-thread state: a synthesized plain external declaration
         * would drop the TLS attribute and silently change addressing */
        verdict = SH_G_BLOCK;
    } else {
        unsigned lk = LLVMGetLinkage(gv);
        bool intern = lk == LLVMInternalLinkage || lk == LLVMPrivateLinkage;
        if (LLVMIsGlobalConstant(gv) && intern) {
            sh_map_put(&st->glob_v, gv, -2);   /* in progress (cycles) */
            sh_map_t done; sh_map_init(&done);
            bool ok = sh_glob_closure_ok(st, LLVMGetInitializer(gv), &done, 0);
            sh_map_free(&done);
            verdict = ok ? SH_G_TRAVEL : SH_G_BLOCK;
            if (st->failed) return SH_G_DECL;
            sh_map_put(&st->glob_v, gv, verdict);   /* overwrites -2 */
            return verdict;
        }
        verdict = SH_G_DECL;
    }
    if (st->failed) return SH_G_DECL;
    sh_map_put(&st->glob_v, gv, verdict);
    return verdict;
}

typedef struct {
    sh_state_t *st;
    sh_map_t    decl_fns;    /* fn -> 1: synthesize declare */
    sh_map_t    decl_globs;  /* global -> 1: synthesize external decl */
    sh_map_t    travel;      /* global -> 1: print definition */
    sh_map_t    body_done;   /* global: closure already marked */
    sh_map_t   *closure_seen;/* shard-level: init closure walked once */
} sh_refs_t;

static bool sh_refs_note_fn(sh_refs_t *r, LLVMValueRef f);
static void sh_link_rec(sh_state_t *st, LLVMValueRef v, bool is_glob) {
    if (st->link_n == st->link_cap) {
        st->link_cap = st->link_cap ? st->link_cap * 2 : 64;
        st->link_recs = (sh_link_rec_t *)realloc(
            st->link_recs, (size_t)st->link_cap * sizeof(*st->link_recs));
    }
    st->link_recs[st->link_n].fn = is_glob ? NULL : v;
    st->link_recs[st->link_n].glob = is_glob ? v : NULL;
    st->link_recs[st->link_n].old_linkage = LLVMGetLinkage(v);
    st->link_n++;
}
static bool sh_link_glob(sh_state_t *st, LLVMValueRef gv) {
    if (sh_map_get(&st->ext_globs, gv) >= 0) return true;
    if (!sh_map_put(&st->ext_globs, gv, 1)) return false;
    sh_link_rec(st, gv, true);
    LLVMSetLinkage(gv, LLVMExternalLinkage);
    return true;
}
static bool sh_refs_note_global(sh_refs_t *r, LLVMValueRef gv);

/* A traveling global is DUPLICATED into the shard: its initializer text
 * references every symbol it closes over from the shard's copy too, so any
 * function it touches must gain an external declaration in the coordinator
 * (the coordinator's own copy of the global keeps pointing at it) and a
 * declare line in this shard's fragment. */
static void sh_mark_closure_fns(sh_state_t *st, sh_refs_t *r, LLVMValueRef gv) {
    /* no cross-shard memo here: the fn refs it finds must land in EVERY
     * referencing shard's declare set (needs_decl itself is idempotent) */
    sh_map_t walk_done; sh_map_init(&walk_done);
    sh_cw_item_t *stack = (sh_cw_item_t *)malloc(64 * sizeof(*stack));
    if (!stack) { sh_fail(st, "out of memory"); return; }
    int sn = 1, scap = 64;
    stack[0].v = LLVMGetInitializer(gv); stack[0].depth = 0;
    while (sn > 0) {
        sh_cw_item_t it = stack[--sn];
        LLVMValueRef v = it.v;
        if (!v || it.depth > 32) continue;
        if (sh_map_get(&walk_done, v) >= 0) continue;
        sh_map_put(&walk_done, v, 1);
        if (LLVMIsAFunction(v)) {
            if (sh_map_get(&st->members_all, v) >= 0) {
                sh_map_put(&st->needs_decl, v, 1);
                /* the referencing shard's declare set: pass 2 emits declares
                 * from r->decl_fns, not from the state-level needs_decl, and
                 * skips local members at emission time */
                if (sh_map_get(&r->decl_fns, v) < 0)
                    sh_map_put(&r->decl_fns, v, 1);
            } else
                sh_refs_note_fn(r, v);
            continue;
        }
        if (LLVMIsAGlobalVariable(v)) {
            if (LLVMIsDeclaration(v)) continue;
            int gvv = sh_map_get(&st->glob_v, v) >= 0
                          ? sh_global_verdict(st, v) : SH_G_DECL;
            if (sn + 2 > scap) {
                scap *= 2;
                sh_cw_item_t *ns = (sh_cw_item_t *)realloc(
                    stack, (size_t)scap * sizeof(*ns));
                if (!ns) { sh_fail(st, "out of memory"); break; }
                stack = ns;
            }
            if (gvv == SH_G_TRAVEL) {
                /* nested travel global: its verbatim definition must be
                 * printed in this fragment too, or the text references it
                 * undefined */
                if (sh_map_get(&r->travel, v) < 0)
                    sh_map_put(&r->travel, v, 1);
            } else {
                /* DECL: needs an external-global declaration; BLOCK: fails */
                if (!sh_refs_note_global(r, v)) { break; }
                continue;
            }
            stack[sn].v = v; stack[sn].depth = it.depth + 1; sn++;
            stack[sn].v = LLVMGetInitializer(v);
            stack[sn].depth = it.depth + 1; sn++;
            continue;
        }
        /* aggregate constants (ConstantStruct/ConstantArray literal
         * initializers) are NOT ConstantExpr but carry fn/global pointers */
        if (LLVMIsAConstant(v)) {
            unsigned nop = LLVMGetNumOperands(v);
            for (unsigned i = 0; i < nop; i++) {
                if (sn + 1 > scap) {
                    scap *= 2;
                    sh_cw_item_t *ns = (sh_cw_item_t *)realloc(
                        stack, (size_t)scap * sizeof(*ns));
                    if (!ns) { sh_fail(st, "out of memory"); break; }
                    stack = ns;
                }
                stack[sn].v = LLVMGetOperand(v, (int)i);
                stack[sn].depth = it.depth + 1; sn++;
            }
        }
    }
    free(stack);
    sh_map_free(&walk_done);
}

/* ---- trace breadcrumbs (ZAN_SHARD_TRACE=1) --------------------------------- */
static time_t sh_trace_t0(void) {
    static time_t t0 = 0;
    if (!t0) t0 = time(NULL);
    return t0;
}
static int sh_trace_on(void) {
    static int on = -1;
    if (on < 0) { const char *e = getenv("ZAN_SHARD_TRACE"); on = e && *e == '1'; }
    return on;
}
#define SH_TRACE(...)     do { if (sh_trace_on()) { \
        fprintf(stderr, "shard-trace [%lus]: ", \
                (unsigned long)(time(NULL) - sh_trace_t0())); \
        fprintf(stderr, __VA_ARGS__); } } while (0)

/* ---- body reference walk --------------------------------------------------- */
/* Every function/global operand of a member body must be resolvable inside
 * the shard: functions and globals route through their verdicts. A member of
 * another shard needs an external declare (it is emitted with external
 * linkage); an internal non-member body stays in the coordinator and is
 * EXPORTED on this reference — its mangled name is unique, so internal →
 * external linkage is a pure visibility change with no collision risk. */
static bool sh_refs_note_fn(sh_refs_t *r, LLVMValueRef f) {
    sh_state_t *st = r->st;
    if (sh_map_get(&st->members_all, f) >= 0) {
        /* member of another shard: it will be emitted with external
         * linkage, so this shard declares it */
        sh_map_put(&st->needs_decl, f, 1);
        if (sh_map_get(&r->decl_fns, f) < 0)
            if (!sh_map_put(&r->decl_fns, f, 1)) { sh_fail(st, "out of memory"); return false; }
        return true;
    }
    int v = sh_fn_verdict(st, f);
    if (v == SH_F_EXT) {
        /* export the coordinator-side body so the shard's declare resolves;
         * recorded for rollback like every other linkage change */
        if (sh_map_get(&st->ext_fns, f) < 0) {
            if (!sh_map_put(&st->ext_fns, f, 1)) { sh_fail(st, "out of memory"); return false; }
            sh_link_rec(st, f, false);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
    } else if (v == SH_F_BLOCK) {
        sh_fail(st, "body references unshardable fn '%s'",
                LLVMGetValueName(f));
        return false;
    }
    /* every non-member verdict needs a declare line in this fragment */
    if (sh_map_get(&r->decl_fns, f) < 0)
        if (!sh_map_put(&r->decl_fns, f, 1)) { sh_fail(st, "out of memory"); return false; }
    return true;
}

static bool sh_refs_note_global(sh_refs_t *r, LLVMValueRef gv) {
    sh_state_t *st = r->st;
    int v = sh_global_verdict(st, gv);
    if (v == SH_G_BLOCK) {
        sh_fail(st, "fn references unshardable global '%s'",
                LLVMGetValueName(gv));
        return false;
    }
    if (v == SH_G_TRAVEL) {
        if (sh_map_get(&r->travel, gv) < 0)
            if (!sh_map_put(&r->travel, gv, 1)) { sh_fail(st, "out of memory"); return false; }
        /* one closure walk per shard: the results land in this shard's
         * decl sets; a state-level memo would starve the later shards */
        if (!r->closure_seen || sh_map_get(r->closure_seen, gv) < 0) {
            if (r->closure_seen) sh_map_put(r->closure_seen, gv, 1);
            sh_mark_closure_fns(st, r, gv);
        }
    } else {
        /* DECL global: the shard gets an external declaration and the
         * coordinator copy must export the symbol — internal linkage would
         * leave the shard's .refptr relocation undefined at link time */
        if (sh_map_get(&r->decl_globs, gv) < 0)
            if (!sh_map_put(&r->decl_globs, gv, 1)) { sh_fail(st, "out of memory"); return false; }
        if (LLVMIsDeclaration(gv)) return true;
        if (!sh_link_glob(st, gv)) { sh_fail(st, "out of memory"); return false; }
    }
    return true;
}

static bool sh_refs_walk_value(sh_refs_t *r, LLVMValueRef v, int depth) {
    if (!v || depth > 32) return true;
    sh_state_t *st = r->st;
    if (LLVMIsAFunction(v)) return sh_refs_note_fn(r, v);
    if (LLVMIsAGlobalVariable(v)) return sh_refs_note_global(r, v);
    if (LLVMIsAGlobalAlias(v)) {
        sh_fail(st, "reference to alias '%s'", LLVMGetValueName(v));
        return false;
    }
    /* constant aggregates (struct/array literals) are not ConstantExpr but
     * carry the same fn/global pointers; fns/globals/aliases returned above,
     * and instructions never arrive through an operand walk */
    if (LLVMIsAConstant(v)) {
        unsigned nop = LLVMGetNumOperands(v);
        for (unsigned i = 0; i < nop; i++)
            if (!sh_refs_walk_value(r, LLVMGetOperand(v, (int)i), depth + 1))
                return false;
    }
    return true;
}

static bool sh_refs_scan_fn(sh_refs_t *r, LLVMValueRef fn) {
    sh_state_t *st = r->st;
    /* NOTE: no LLVMGetPersonalityFn here — it segfaults in this LLVM build
     * even on a plain function handle. Eligible Zan bodies use the setjmp
     * EH path and carry no personality; if one ever does, its printed
     * fragment references an undeclared symbol and the parse fails into the
     * clean single-module fallback. */
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
         bb = LLVMGetNextBasicBlock(bb)) {
        for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
             in = LLVMGetNextInstruction(in)) {
            unsigned nop = LLVMGetNumOperands(in);
            for (unsigned i = 0; i < nop; i++) {
                /* a direct call's callee is its last operand; the generic
                 * walk covers it (a non-call fn operand cannot happen for
                 * eligible fns — address-taken ones never got here) */
                if (!sh_refs_walk_value(r, LLVMGetOperand(in, (int)i), 0))
                    return false;
            }
        }
    }
    (void)st;
    return true;
}

/* ---- planning -------------------------------------------------------------- */
typedef struct {
    int         *idx;        /* manifest indices */
    int          n;
    long long    insns;
    const char  *minname;
} sh_comp_t;

typedef struct {
    int mf_i;                /* manifest index */
    LLVMValueRef fn;
    int  comp;
    bool needs_decl;
} sh_member_t;

static int sh_cmp_comp(const void *a, const void *b) {
    const sh_comp_t *x = (const sh_comp_t *)a, *y = (const sh_comp_t *)b;
    if (x->insns != y->insns) return y->insns > x->insns ? 1 : -1;
    return strcmp(x->minname, y->minname);
}

static int sh_cmp_name_ref(const void *a, const void *b) {
    LLVMValueRef x = *(LLVMValueRef *)a, y = *(LLVMValueRef *)b;
    return strcmp(LLVMGetValueName(x), LLVMGetValueName(y));
}

/* ---- module text carving --------------------------------------------------- */
/* LLVMPrintValueToString builds a module-wide SlotTracker on every call, so
 * printing member bodies one by one costs O(members x module size) — 331s of
 * the ~400s IDE shard run. Print the module text once and carve each member's
 * `define` block out of it. */
typedef struct { char *name; size_t off, len; } sh_span_t;

static int sh_span_cmp(const void *a, const void *b) {
    return strcmp(((const sh_span_t *)a)->name, ((const sh_span_t *)b)->name);
}

/* Index every `define` block of the printed module text; spans end at the
 * column-0 closing '}' (string literals never contain real newlines, so the
 * marker is unambiguous). Names are matched verbatim against
 * LLVMGetValueName; quoted/escaped names (not produced by Zan mangling) fail
 * the lookup and poison the shard — deterministic fallback. */
static sh_span_t *sh_index_module_text(const char *txt, int *out_n) {
    int cap = 256, n = 0;
    sh_span_t *sp = (sh_span_t *)malloc((size_t)cap * sizeof(*sp));
    if (!sp) return NULL;
    for (const char *p = txt; *p; ) {
        if (strncmp(p, "define", 6) == 0 && (p == txt || p[-1] == '\n')) {
            /* the name's '@' comes first; the first '(' of the line may sit
             * earlier (e.g. -O2's `range(i32 0, N)` return attribute) */
            const char *at = strchr(p, '@');
            const char *lp = at ? strchr(at, '(') : NULL;
            if (at && lp) {
                const char *ns, *ne = NULL;
                if (at[1] == '"') {
                    ns = at + 2;
                    for (ne = ns; *ne && *ne != '"' && *ne != '\n'; ne++) {}
                } else {
                    ns = at + 1;
                    for (ne = ns; ne < lp && *ne != ' ' && *ne != '\n'; ne++) {}
                }
                int okname = (at[1] == '"') ? (*ne == '"') : (ne > ns);
                if (okname && ns < ne) {
                    if (n == cap) {
                        cap *= 2;
                        sh_span_t *np = (sh_span_t *)realloc(
                            sp, (size_t)cap * sizeof(*sp));
                        if (!np) break;
                        sp = np;
                    }
                    sp[n].name = (char *)malloc((size_t)(ne - ns) + 1);
                    if (!sp[n].name) break;
                    memcpy(sp[n].name, ns, (size_t)(ne - ns));
                    sp[n].name[ne - ns] = '\0';
                    sp[n].off = (size_t)(p - txt);
                    const char *e = strstr(p, "\n}\n");
                    if (!e) { free(sp[n].name); break; }
                    sp[n].len = (size_t)(e + 3 - p);
                    n++;
                }
            }
        }
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    qsort(sp, (size_t)n, sizeof(*sp), sh_span_cmp);
    *out_n = n;
    return sp;
}

static const sh_span_t *sh_span_find(const sh_span_t *sp, int n,
                                     const char *name) {
    sh_span_t key;
    key.name = (char *)name;
    return (const sh_span_t *)bsearch(&key, sp, (size_t)n, sizeof(*sp),
                                      sh_span_cmp);
}

/* ---- type harvest ----------------------------------------------------------- */
/* Scan .ll text for %identifiers and emit `= type {...}` lines for the ones
 * the coordinator's type table knows (named structs). GEP source element
 * types exist ONLY in printed text, so this — not the C API — is the
 * complete source of the shard's type table. Post-order: a struct's element
 * strings are scanned (and their types emitted) before the struct's own
 * line, so the parser never sees a forward reference. */
static bool sh_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '$' || c == '.' ||
           c == '-';
}

static void sh_harvest_types(sh_state_t *st, sh_sbuf_t *types, const char *text) {
    for (const char *p = text; *p; p++) {
        if (*p != '%') continue;
        bool quoted = false;
        const char *s = p + 1;
        if (*s == '"') {
            quoted = true;
            s++;
        } else if (!sh_ident_char(*s) || (*s >= '0' && *s <= '9')) {
            continue;
        }
        char name[256];
        size_t n = 0;
        if (quoted) {
            while (*s && *s != '"' && n < sizeof(name) - 1) name[n++] = *s++;
            if (*s == '"') s++;
        } else {
            while (sh_ident_char(*s) && n < sizeof(name) - 1) name[n++] = *s++;
        }
        name[n] = '\0';
        LLVMTypeRef ty = LLVMGetTypeByName2(st->g->ctx, name);
        if (!ty || sh_map_get(&st->type_done, ty) >= 0) continue;
        if (LLVMGetTypeKind(ty) != LLVMStructTypeKind) continue;
        sh_map_put(&st->type_done, ty, 1);
        if (LLVMIsOpaqueStruct(ty)) {
            char line[512];
            if (quoted) {
                snprintf(line, sizeof(line), "%%\"%s\" = type opaque\n", name);
            } else {
                snprintf(line, sizeof(line), "%%%s = type opaque\n", name);
            }
            sh_sb_puts(types, line);
            continue;
        }
        /* children first: element strings may name further structs; elems
         * lives until after the emit loop below reuses it */
        unsigned ne = LLVMCountStructElementTypes(ty);
        LLVMTypeRef *elems = NULL;
        if (ne) {
            elems = (LLVMTypeRef *)malloc((size_t)ne * sizeof(*elems));
            if (!elems) { sh_fail(st, "out of memory"); return; }
            LLVMGetStructElementTypes(ty, elems);
            for (unsigned i = 0; i < ne; i++) {
                char *es = LLVMPrintTypeToString(elems[i]);
                sh_harvest_types(st, types, es);
                LLVMDisposeMessage(es);
            }
        }
        char line[512];
        if (quoted) {
            snprintf(line, sizeof(line), "%%\"%s\" = type %s{ ", name,
                     LLVMIsPackedStruct(ty) ? "<" : "");
        } else {
            snprintf(line, sizeof(line), "%%%s = type %s{ ", name,
                     LLVMIsPackedStruct(ty) ? "<" : "");
        }
        sh_sb_puts(types, line);
        if (ne) {
            for (unsigned i = 0; i < ne; i++) {
                char *es = LLVMPrintTypeToString(elems[i]);
                sh_sb_puts(types, es);
                LLVMDisposeMessage(es);
                if (i + 1 < ne) sh_sb_puts(types, ", ");
            }
            free(elems);
        }
        sh_sb_puts(types, LLVMIsPackedStruct(ty) ? " }>\n" : " }\n");
    }
}

/* ---- synthesized declarations ------------------------------------------------ */
static void sh_emit_fn_decl(sh_sbuf_t *b, LLVMValueRef f) {
    LLVMTypeRef ft = LLVMGlobalGetValueType(f);
    LLVMTypeRef rt = LLVMGetReturnType(ft);
    char *rts = LLVMPrintTypeToString(rt);
    sh_sb_puts(b, "declare ");
    sh_sb_puts(b, rts);
    LLVMDisposeMessage(rts);
    sh_sb_puts(b, " @");
    sh_sb_puts(b, LLVMGetValueName(f));
    sh_sb_puts(b, "(");
    unsigned np = LLVMCountParamTypes(ft);
    LLVMTypeRef *pts = NULL;
    if (np) {
        pts = (LLVMTypeRef *)malloc((size_t)np * sizeof(*pts));
        if (pts) LLVMGetParamTypes(ft, pts);
    }
    for (unsigned i = 0; i < np && pts; i++) {
        char *ps = LLVMPrintTypeToString(pts[i]);
        sh_sb_puts(b, ps);
        LLVMDisposeMessage(ps);
        if (i + 1 < np || LLVMIsFunctionVarArg(ft)) sh_sb_puts(b, ", ");
    }
    free(pts);
    if (LLVMIsFunctionVarArg(ft)) sh_sb_puts(b, "...");
    sh_sb_puts(b, ")\n");
}

static void sh_emit_global_decl(sh_state_t *st, sh_sbuf_t *b, LLVMValueRef gv) {
    if (!LLVMIsDeclaration(gv)) {
        /* defined in the coordinator: rewrite the definition into an
         * external declaration (keeps `constant` for constants; alignment
         * and initializer stay coordinator-only) */
        sh_sb_puts(b, "@");
        sh_sb_puts(b, LLVMGetValueName(gv));
        sh_sb_puts(b, " = external ");
        sh_sb_puts(b, LLVMIsGlobalConstant(gv) ? "constant " : "global ");
        char *ts = LLVMPrintTypeToString(LLVMGlobalGetValueType(gv));
        sh_sb_puts(b, ts);
        LLVMDisposeMessage(ts);
        sh_sb_puts(b, "\n");
        return;
    }
    /* coordinator declaration: print verbatim (thread_local, addrspace,
     * dllimport all round-trip) */
    char *txt = LLVMPrintValueToString(gv);
    if (txt) { sh_sb_puts(b, txt); sh_sb_puts(b, "\n"); LLVMDisposeMessage(txt); }
    (void)st;
}

/* ---- fragment build + emit ---------------------------------------------------- */
static LLVMTargetMachineRef sh_make_tm(sh_state_t *st) {
    LLVMTargetMachineRef tm = NULL;
    if (zan_bind_target_layout(st->g, &tm) != ZAN_OK) return NULL;
    return tm;
}

static void sh_ensure_parent_dir(const char *path) {
    if (!path) return;
    char dir[1024];
    size_t len = strlen(path);
    if (len >= sizeof(dir)) return;
    memcpy(dir, path, len + 1);
    char *p = dir;
    while (*p) {
        if ((*p == '/' || *p == '\\') && p > dir) {
            char sep = *p;
            *p = '\0';
#ifdef _WIN32
            CreateDirectoryA(dir, NULL);
#else
            mkdir(dir, 0755);
#endif
            *p = sep;
        }
        p++;
    }
}

static bool sh_emit_one(sh_state_t *st, sh_sbuf_t *frag, LLVMTargetMachineRef tm,
                        const char *path, char *errbuf, size_t errsz) {
    zan_irgen_t *g = st->g;
    sh_ensure_parent_dir(path);
    const char *dump = getenv("ZAN_SHARD_DUMP");
    if (dump && *dump) {
        FILE *df = fopen(dump, "wb");
        if (df) {
            fwrite(frag->p ? frag->p : "", 1, frag->n, df);
            fclose(df);
        }
    }
    LLVMContextRef ctx = LLVMContextCreate();
    if (!ctx) { snprintf(errbuf, errsz, "context create failed"); return false; }
    SH_TRACE("shard emit: frag %d bytes -> parse\n", (int)frag->n);
    LLVMMemoryBufferRef mb = LLVMCreateMemoryBufferWithMemoryRangeCopy(
        frag->p ? frag->p : "", frag->n, "zan-shard");
    LLVMModuleRef mod = NULL;
    char *perr = NULL;
    if (LLVMParseIRInContext(ctx, mb, &mod, &perr)) {
        snprintf(errbuf, errsz, "parse: %.160s", perr ? perr : "?");
        if (perr) LLVMDisposeMessage(perr);
        LLVMContextDispose(ctx);
        return false;
    }
    const char *triple = LLVMGetTarget(g->mod);
    if (triple && *triple) LLVMSetTarget(mod, triple);
    LLVMSetDataLayout(mod, LLVMGetDataLayoutStr(g->mod));
    char *vmsg = NULL;
    SH_TRACE("shard emit: parse ok -> verify\n");
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &vmsg)) {
        snprintf(errbuf, errsz, "verify: %.160s", vmsg ? vmsg : "?");
        if (vmsg) LLVMDisposeMessage(vmsg);
        LLVMDisposeModule(mod);
        LLVMContextDispose(ctx);
        return false;
    }
    if (vmsg) LLVMDisposeMessage(vmsg);
    if (g->obfuscate_strings || getenv("ZAN_SHARD_OPT")) {
        zan_opt_run_passes_on_module(mod, tm, g->obfuscate_strings ? ZAN_OPT_SIZE : ZAN_OPT_FULL);
    }
    char *eerr = NULL;
    SH_TRACE("shard emit: verify ok -> codegen\n");
    if (LLVMTargetMachineEmitToFile(tm, mod, path, LLVMObjectFile, &eerr)) {
        snprintf(errbuf, errsz, "emit: %.160s", eerr ? eerr : "?");
        if (eerr) LLVMDisposeMessage(eerr);
        LLVMDisposeModule(mod);
        LLVMContextDispose(ctx);
        return false;
    }
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
    return true;
}

/* ---- move (rename + decl + RAUW + delete), fully rollback-able -------------- */
static bool sh_unique_fn_name(zan_irgen_t *g, const char *base, const char *sfx,
                              int *counter, char *out, size_t outsz) {
    for (int i = 0; i < 10000; i++) {
        snprintf(out, outsz, "%s%s#%d", base, sfx, (*counter)++);
        if (!LLVMGetNamedFunction(g->mod, out) &&
            !LLVMGetNamedGlobal(g->mod, out))
            return true;
    }
    return false;
}

/* ---- streaming shard harvesting & instant eviction ---- */
static void sh_harvest_symbols_from_text(sh_state_t *st, const char *text,
                                        sh_map_t *decl_fns, sh_map_t *decl_globs,
                                        sh_map_t *local_fns) {
    for (const char *p = text; *p; p++) {
        if (*p != '@') continue;
        bool quoted = false;
        const char *s = p + 1;
        if (*s == '"') {
            quoted = true;
            s++;
        } else if (!sh_ident_char(*s) || (*s >= '0' && *s <= '9')) {
            continue;
        }
        char name[512];
        size_t n = 0;
        if (quoted) {
            while (*s && *s != '"' && n < sizeof(name) - 1) name[n++] = *s++;
            if (*s == '"') s++;
        } else {
            while (sh_ident_char(*s) && n < sizeof(name) - 1) name[n++] = *s++;
        }
        name[n] = '\0';
        LLVMValueRef fn = LLVMGetNamedFunction(st->g->mod, name);
        if (fn) {
            if (sh_map_get(local_fns, fn) < 0 && sh_map_get(decl_fns, fn) < 0) {
                sh_map_put(decl_fns, fn, 1);
            }
            continue;
        }
        LLVMValueRef gv = LLVMGetNamedGlobal(st->g->mod, name);
        if (gv) {
            if (sh_map_get(decl_globs, gv) < 0) {
                sh_map_put(decl_globs, gv, 1);
            }
            continue;
        }
    }
}

static int sh_run_streaming_shards(zan_irgen_t *g, const char *obj_base, char ***out_objs) {
    int nshard = g->streaming_shard_count;
    char **objs = (char **)calloc((size_t)nshard, sizeof(*objs));
    if (!objs) return -1;

    sh_state_t st;
    memset(&st, 0, sizeof(st));
    st.g = g;
    LLVMTargetMachineRef tm = sh_make_tm(&st);
    if (!tm) {
        free(objs);
        return -1;
    }

    fprintf(stderr, "streaming-shard: emitting %d shard objects on the fly\n", nshard);

    for (int s = 0; s < nshard; s++) {
        struct zan_shard_buf *sb = &g->streaming_shards[s];
        if (!sb->text || sb->len == 0) continue;

        sh_sbuf_t gdecls = {0}, types = {0}, frag = {0};
        sh_map_t decl_fns, decl_globs, local_fns;
        sh_map_init(&decl_fns); sh_map_init(&decl_globs); sh_map_init(&local_fns);
        sh_map_init(&st.type_done);

        for (int k = 0; k < sb->fn_count; k++) {
            if (sb->fns && sb->fns[k]) {
                sh_map_put(&local_fns, sb->fns[k], 1);
            }
        }
        sh_harvest_symbols_from_text(&st, sb->text, &decl_fns, &decl_globs, &local_fns);

        /* export and emit global declarations */
        for (int i = 0; i <= decl_globs.mask; i++) {
            if (decl_globs.vals[i] > 0) {
                LLVMValueRef gv = decl_globs.keys[i];
                if (!LLVMIsDeclaration(gv)) {
                    LLVMSetLinkage(gv, LLVMExternalLinkage);
                }
                sh_emit_global_decl(&st, &gdecls, gv);
            }
        }

        /* export and emit function declarations */
        for (int i = 0; i <= decl_fns.mask; i++) {
            if (decl_fns.vals[i] > 0) {
                LLVMValueRef fn = decl_fns.keys[i];
                if (!LLVMIsDeclaration(fn)) {
                    LLVMSetLinkage(fn, LLVMExternalLinkage);
                }
                sh_emit_fn_decl(&gdecls, fn);
            }
        }

        /* harvest struct types from bodies and declarations */
        sh_harvest_types(&st, &types, sb->text);
        sh_harvest_types(&st, &types, gdecls.p ? gdecls.p : "");

        /* assemble final fragment */
        sh_sb_puts(&frag, types.p ? types.p : "");
        sh_sb_puts(&frag, gdecls.p ? gdecls.p : "");
        sh_sb_puts(&frag, sb->text);

        char path[1200];
        snprintf(path, sizeof(path), "%s.shard%d.o", obj_base, s);
        char errbuf[256];
        bool ok = sh_emit_one(&st, &frag, tm, path, errbuf, sizeof(errbuf));

        sh_map_free(&local_fns);
        sh_map_free(&decl_fns);
        sh_map_free(&decl_globs);
        sh_map_free(&st.type_done);
        st.type_done.keys = NULL; st.type_done.vals = NULL;
        free(gdecls.p); free(types.p); free(frag.p);

        /* Instant eviction of text buffer: free immediately to drop memory */
        free(sb->text);
        sb->text = NULL;
        sb->len = sb->cap = 0;

        if (!ok) {
            fprintf(stderr, "streaming-shard %d failed: %s\n", s, errbuf);
            for (int k = 0; k <= s; k++) {
                if (objs[k]) { remove(objs[k]); free(objs[k]); }
            }
            free(objs);
            return -1;
        }

        objs[s] = (char *)malloc(strlen(path) + 1);
        if (objs[s]) strcpy(objs[s], path);
    }

    *out_objs = objs;
    return nshard;
}

static int g_harvest_calls = 0;
static int g_harvest_decl = 0;
static int g_harvest_no_bb = 0;
static int g_harvest_comdat = 0;
static int g_harvest_success = 0;

void zan_irgen_shard_harvest_stats(void) {
    fprintf(stderr, "\n=== HARVEST STATS: calls=%d, decl=%d, no_bb=%d, comdat=%d, success=%d ===\n\n",
            g_harvest_calls, g_harvest_decl, g_harvest_no_bb, g_harvest_comdat, g_harvest_success);
}

bool zan_irgen_shard_harvest_fn(zan_irgen_t *g, LLVMValueRef fn) {
    g_harvest_calls++;
    if (!g || !fn || LLVMIsDeclaration(fn)) { g_harvest_decl++; return false; }
    LLVMBasicBlockRef first_bb = LLVMGetFirstBasicBlock(fn);
    if (!first_bb) { g_harvest_no_bb++; return false; }

    LLVMSetLinkage(fn, LLVMExternalLinkage);
    char *fntxt = LLVMPrintValueToString(fn);
    if (!fntxt) return false;

    /* If the function header carries comdat or alias, keep it in coordinator */
    const char *hnl = strchr(fntxt, '\n');
    size_t hlen = hnl ? (size_t)(hnl - fntxt) : strlen(fntxt);
    if (hlen < 4096) {
        char hbuf[4096];
        memcpy(hbuf, fntxt, hlen);
        hbuf[hlen] = '\0';
        if (strstr(hbuf, " comdat($")) {
            g_harvest_comdat++;
            LLVMDisposeMessage(fntxt);
            return false;
        }
    }

    zan_irgen_shard_buf_append(g, fn, fntxt);
    LLVMDisposeMessage(fntxt);

    /* Instant Eviction: delete all basic blocks from coordinator module */
    LLVMBasicBlockRef bb = first_bb;
    while (bb) {
        LLVMBasicBlockRef next_bb = LLVMGetNextBasicBlock(bb);
        LLVMDeleteBasicBlock(bb);
        bb = next_bb;
    }
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    if (LLVMGetFirstBasicBlock(fn) != NULL) {
        static int warn_cnt = 0;
        if (warn_cnt++ < 5) {
            fprintf(stderr, "HARVEST BUG: fn '%s' still has first_bb after delete!\n", LLVMGetValueName(fn));
        }
    }
    g_harvest_success++;
    return true;
}

/* ---- entry ---------------------------------------------------------------- */
/*
 * Returns the number of shard objects written (>= 0, may be 0 = clean
 * fallback), or -1 on an internal error the caller should treat as fatal.
 * `out_objs` receives a malloc'd array of malloc'd path strings.
 */
static void sh_planner_mark(sh_state_t *st, LLVMValueRef v, int depth) {
    if (!v || depth > 8) return;
    if (LLVMIsAFunction(v)) {
        if (sh_map_get(&st->members_all, v) >= 0)
            sh_map_put(&st->needs_decl, v, 1);
        return;
    }
    if (LLVMIsAConstant(v)) {
        unsigned nop = LLVMGetNumOperands(v);
        for (unsigned k = 0; k < nop; k++)
            sh_planner_mark(st, LLVMGetOperand(v, (int)k), depth + 1);
    }
}

/* trace-only: find surviving references to moved-and-renamed bodies inside
 * global initializer constants */
static void sh_trace_scan_const(LLVMValueRef owner, LLVMValueRef v, int depth) {
    if (!v || depth > 8) return;
    if (LLVMIsAFunction(v)) {
        const char *on = LLVMGetValueName(v);
        if (strstr(on, ".zsb$") || strstr(on, ".zsh$") || strstr(on, ".zshx$"))
            SH_TRACE("post-delete ref (global '%s') -> %s [decl=%d lk=%d]\n",
                     LLVMGetValueName(owner), on, LLVMIsDeclaration(v),
                     (int)LLVMGetLinkage(v));
        return;
    }
    if (LLVMIsAConstant(v)) {
        unsigned nop = LLVMGetNumOperands(v);
        for (unsigned k = 0; k < nop; k++)
            sh_trace_scan_const(owner, LLVMGetOperand(v, (int)k), depth + 1);
    }
}

int zan_irgen_shard_run(zan_irgen_t *g, const zan_cg_manifest_t *m,
                        const char *obj_base, char ***out_objs) {
    *out_objs = NULL;
    if (g && g->streaming_shard_count > 0) {
        return sh_run_streaming_shards(g, obj_base, out_objs);
    }
    if (!m || m->fn_count == 0) return 0;

    long long max_fn = 400, max_insn = 80000;
    const char *e;
    if ((e = getenv("ZAN_SHARD_MAX_FN")) && *e) max_fn = atoll(e);
    if ((e = getenv("ZAN_SHARD_MAX_INSN")) && *e) max_insn = atoll(e);

    sh_state_t st;
    memset(&st, 0, sizeof(st));
    st.g = g;
    sh_map_init(&st.fn_v); sh_map_init(&st.glob_v);
    sh_map_init(&st.members_all);
    sh_map_init(&st.needs_decl);
    sh_map_init(&st.ext_globs);
    sh_map_init(&st.ext_fns);

    /* ---- seeds: manifest-eligible user bodies, entry points excluded ---- */
    sh_member_t *mem = (sh_member_t *)calloc((size_t)(m->fn_count ? m->fn_count : 1),
                                             sizeof(*mem));
    int *uf = (int *)calloc((size_t)(m->fn_count ? m->fn_count : 1), sizeof(*uf));
    int nm = 0;
    long long mov_insns = 0;
    int meta_skip = 0;
    for (int i = 0; i < m->fn_count; i++) {
        const zan_mf_fn *F = &m->fns[i];
        if (!F->defined || !F->eligible) continue;
        if (!strcmp(F->name, "Main") || !strcmp(F->name, "main")) continue;
        LLVMValueRef f = LLVMGetNamedFunction(g->mod, F->name);
        if (!f) continue;
        if (sh_has_attached_metadata(f)) {
            meta_skip++;
            continue;
        }
        mem[nm].mf_i = i;
        mem[nm].fn = f;
        mem[nm].comp = nm;
        uf[nm] = nm;
        nm++;
    }
    if (!nm) {
        fprintf(stderr, "shard: no eligible bodies — single module\n");
        free(mem); free(uf);
        sh_map_free(&st.fn_v); sh_map_free(&st.glob_v);
        sh_map_free(&st.members_all);
        sh_map_free(&st.needs_decl);
        return 0;
    }

    /* member lookup: manifest index -> member slot */
    sh_map_t mf2mem; sh_map_init(&mf2mem);
    for (int k = 0; k < nm; k++) sh_map_put(&mf2mem, mem[k].fn, k);

    /* ---- dirty fixpoint: drop members whose reference closure cannot be
     * proven resolvable through external symbols ---- */
    bool changed = true;
    int iter_guard = 0;
    while (changed && iter_guard++ < 64) {
        changed = false;
        for (int k = 0; k < nm; k++) {
            sh_member_t *M = &mem[k];
            if (M->comp < 0) continue;   /* already dirty */
            const zan_mf_fn *F = &m->fns[M->mf_i];
            bool dirty = false;
            for (int c = 0; c < F->call_cnt && !dirty; c++) {
                int ci = F->calls[c];
                LLVMValueRef cf = LLVMGetNamedFunction(g->mod, m->fns[ci].name);
                if (!cf) { dirty = true; break; }
                int slot = sh_map_get(&mf2mem, cf);
                if (slot >= 0) {
                    if (mem[slot].comp < 0) dirty = true;
                } else if (sh_fn_verdict(&st, cf) == SH_F_BLOCK) {
                    dirty = true;
                }
            }
            for (int x = 0; x < F->ext_cnt && !dirty; x++) {
                LLVMValueRef xf = LLVMGetNamedFunction(g->mod, F->exts[x]);
                if (xf && sh_fn_verdict(&st, xf) == SH_F_BLOCK) dirty = true;
            }
            for (int x = 0; x < F->glob_cnt && !dirty; x++) {
                LLVMValueRef gv2 = LLVMGetNamedGlobal(g->mod, F->globs[x]);
                if (!gv2 || sh_global_verdict(&st, gv2) == SH_G_BLOCK) dirty = true;
            }
            if (dirty) {
                M->comp = -1;
                changed = true;
            }
        }
    }
    int dropped = 0;
    for (int k = 0; k < nm; k++) {
        if (mem[k].comp < 0) {
            sh_map_put(&mf2mem, mem[k].fn, -1);
            dropped++;
        } else {
            sh_map_put(&st.members_all, mem[k].fn, 1);
            mov_insns += m->fns[mem[k].mf_i].insns;
        }
    }

    /* Direct bin-packing: do not union-find call edges into a single giant
     * connected component that concentrates 90% of the project in shard 0.
     * Inter-shard calls are safely lowered via external declarations. */
    for (int k = 0; k < nm; k++) {
        if (mem[k].comp < 0) continue;
        mem[k].comp = k;
    }

    /* ---- components ---- */
    sh_map_t root2comp; sh_map_init(&root2comp);
    sh_comp_t *comps = (sh_comp_t *)calloc((size_t)nm, sizeof(*comps));
    int ncomp = 0;
    for (int k = 0; k < nm; k++) {
        if (mem[k].comp < 0) continue;
        int root = mem[k].comp;
        int ci = sh_map_get(&root2comp, mem[root].fn);
        if (ci < 0) {
            ci = ncomp++;
            sh_map_put(&root2comp, mem[root].fn, ci);
            comps[ci].idx = (int *)malloc((size_t)(nm - k + 1) * sizeof(int));
            comps[ci].n = 0;
            comps[ci].insns = 0;
            comps[ci].minname = LLVMGetValueName(mem[k].fn);
        }
        comps[ci].idx[comps[ci].n++] = k;
        comps[ci].insns += m->fns[mem[k].mf_i].insns;
        const char *nm2 = LLVMGetValueName(mem[k].fn);
        if (strcmp(nm2, comps[ci].minname) < 0) comps[ci].minname = nm2;
    }
    for (int c = 0; c < ncomp; c++)
        comps[c].idx = (int *)realloc(comps[c].idx,
                                      (size_t)comps[c].n * sizeof(int));
    qsort(comps, (size_t)ncomp, sizeof(*comps), sh_cmp_comp);

    /* ---- packing: whole components into shards ---- */
    int nshard = 0;
    long long cur_fn = 0, cur_insn = 0;
    int *shard_of_comp = (int *)malloc((size_t)(ncomp ? ncomp : 1) * sizeof(int));
    for (int c = 0; c < ncomp; c++) {
        if (nshard == 0 || cur_fn + comps[c].n > max_fn ||
            cur_insn + comps[c].insns > max_insn) {
            nshard++;
            cur_fn = 0; cur_insn = 0;
        }
        shard_of_comp[c] = nshard - 1;
        cur_fn += comps[c].n;
        cur_insn += comps[c].insns;
    }
    sh_map_free(&root2comp);
    free(uf);

    fprintf(stderr,
            "shard: planning %d eligible -> %d fns / %lld insns movable "
            "(%d dropped as unshardable, %d metadata-attached), %d objects\n",
            nm + dropped + meta_skip, nm - dropped, mov_insns, dropped,
            meta_skip, nshard);
    if (nshard <= 1) {
        /* 0 or 1 shard: no benefit from splitting across files, keep single module */
        for (int c = 0; c < ncomp; c++) free(comps[c].idx);
        free(comps); free(shard_of_comp); free(mem);
        sh_map_free(&mf2mem);
        sh_map_free(&st.fn_v); sh_map_free(&st.glob_v);
        sh_map_free(&st.members_all);
        sh_map_free(&st.needs_decl);
        return 0;
    }

    /* member -> shard via comp */
    for (int c = 0; c < ncomp; c++)
        for (int j = 0; j < comps[c].n; j++)
            mem[comps[c].idx[j]].comp = shard_of_comp[c];
    /* dirty members keep comp == -1 */

    /* Coordinator-side callers force an external declaration: Main, generic
     * specs, static ctors — every defined body that STAYS in the coordinator
     * module and references a member (direct call OR address taken into a
     * table) keeps that member's symbol alive. Scanning the actual operands
     * (not the manifest's call edges) also covers fn-pointer stores; same-
     * shard callers are exempt: both bodies move together and the use
     * disappears with them. Member callers' edges come from pass 1. */
    for (int i = 0; i < m->fn_count; i++) {
        const zan_mf_fn *F = &m->fns[i];
        if (!F->defined) continue;
        LLVMValueRef caller = LLVMGetNamedFunction(g->mod, F->name);
        if (!caller) continue;
        int cslot = sh_map_get(&mf2mem, caller);
        if (cslot >= 0 && mem[cslot].comp >= 0) continue;
        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(caller); bb;
             bb = LLVMGetNextBasicBlock(bb)) {
            for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
                 in = LLVMGetNextInstruction(in)) {
                if (LLVMGetInstructionOpcode(in) == LLVMLandingPad)
                    continue; /* personality ref is not an address escape */
                unsigned nop = LLVMGetNumOperands(in);
                for (unsigned k = 0; k < nop; k++)
                    sh_planner_mark(&st, LLVMGetOperand(in, (int)k), 0);
            }
        }
    }

    /* Coordinator-side member references must be marked BEFORE linkage, and
     * fn pointers reach bodies not only bare but wrapped in constant
     * expressions (bitcast into dispatch tables), so the operand walk
     * descends constants. */

    /* ---- global-initializer closure: vtables, reflection tables and const
     * dispatch tables keep member addresses alive from the COORDINATOR side.
     * Members referenced only from a global would never be marked (pass 1/2
     * scan shard bodies, the planner scans fn operands) and would be deleted
     * while the table still points at them — mark them before the linkage
     * externalization so the shard text prints them as `define external`. */
    {
        sh_refs_t scratch;
        memset(&scratch, 0, sizeof(scratch));
        scratch.st = &st;
        sh_map_init(&scratch.decl_fns); sh_map_init(&scratch.decl_globs);
        sh_map_init(&scratch.travel); sh_map_init(&scratch.body_done);
        scratch.closure_seen = &scratch.body_done;
        for (LLVMValueRef gv = LLVMGetFirstGlobal(g->mod); gv;
             gv = LLVMGetNextGlobal(gv)) {
            if (!LLVMGetInitializer(gv)) continue;
            sh_mark_closure_fns(&st, &scratch, gv);
            if (st.failed) break;
        }
        sh_map_free(&scratch.decl_fns); sh_map_free(&scratch.decl_globs);
        sh_map_free(&scratch.travel); sh_map_free(&scratch.body_done);
    }

    /* ---- pass 1: reference walks per shard (fills needs_decl / demotions
     * only through verdicts; actual linkage changes deferred to pass 2) ---- */
    for (int s = 0; s < nshard && !st.failed; s++) {
        sh_refs_t refs;
        memset(&refs, 0, sizeof(refs));
        refs.st = &st;
        sh_map_init(&refs.decl_fns); sh_map_init(&refs.decl_globs);
        sh_map_init(&refs.travel); sh_map_init(&refs.body_done);
        refs.closure_seen = &refs.body_done;
        for (int c = 0; c < ncomp; c++) {
            if (shard_of_comp[c] != s) continue;
            for (int j = 0; j < comps[c].n; j++) {
                LLVMValueRef fn = mem[comps[c].idx[j]].fn;
                if (!sh_refs_scan_fn(&refs, fn)) goto pass1_done;
            }
        }
    pass1_done:
        SH_TRACE("pass1 done\n");
        sh_map_free(&refs.decl_fns); sh_map_free(&refs.decl_globs);
        sh_map_free(&refs.travel); sh_map_free(&refs.body_done);
    }

    int rc = 0;
    char **objs = NULL;
    LLVMTargetMachineRef tm = NULL;
    if (st.failed) {
        /* pass 1 may already have externalized globals — undo before the
         * single-module continue */
        for (int i = 0; i < st.link_n; i++)
            LLVMSetLinkage(st.link_recs[i].glob ? st.link_recs[i].glob
                                               : st.link_recs[i].fn,
                           st.link_recs[i].old_linkage);
        fprintf(stderr, "shard: %s — falling back to single module\n",
                st.reason);
        goto cleanup_planning;
    }

    /* ---- linkage: members with outside references go external (recorded),
     * internal __zan_ helpers referenced by shards get demoted ---- */
    for (int k = 0; k < nm; k++) {
        if (mem[k].comp < 0) continue;
        if (sh_map_get(&st.needs_decl, mem[k].fn) >= 0) {
            sh_link_rec(&st, mem[k].fn, false);
            LLVMSetLinkage(mem[k].fn, LLVMExternalLinkage);
        }
    }
    /* demote referenced internal __zan_* helpers (verdict memo has them) */
    for (int i = 0; i <= st.fn_v.mask; i++) {
        if (st.fn_v.vals[i] != SH_F_DEMOTE) continue;
        LLVMValueRef f = st.fn_v.keys[i];
        sh_link_rec(&st, f, false);
        LLVMSetLinkage(f, LLVMExternalLinkage);
    }

    SH_TRACE("linkage done (%d recs)\n", st.link_n);
    /* ---- pass 2: assemble, parse, verify, emit ---- */
    tm = sh_make_tm(&st);
    if (!tm) {
        snprintf(st.reason, sizeof(st.reason), "cannot create target machine");
        st.failed = true;
    }
    objs = (char **)calloc((size_t)nshard, sizeof(*objs));
    if (!objs) { snprintf(st.reason, sizeof(st.reason), "out of memory"); st.failed = true; }

    for (int s = 0; s < nshard && !st.failed; s++) {
        SH_TRACE("shard %d: assembling\n", s);
        sh_sbuf_t bodies = {0}, gdecls = {0}, types = {0}, frag = {0};
        sh_map_t decl_fns, decl_globs, travel;
        sh_map_init(&decl_fns); sh_map_init(&decl_globs); sh_map_init(&travel);
        sh_map_init(&st.type_done);

        bool ok = true;
        /* collect this shard's local members */
        sh_map_t local; sh_map_init(&local);
        sh_map_t closure_memo; sh_map_init(&closure_memo);
        for (int c = 0; c < ncomp && ok; c++) {
            if (shard_of_comp[c] != s) continue;
            for (int j = 0; j < comps[c].n; j++)
                if (!sh_map_put(&local, mem[comps[c].idx[j]].fn, 1)) ok = false;
        }

        /* reference scan + body assembly */
        for (int c = 0; c < ncomp && ok; c++) {
            if (shard_of_comp[c] != s) continue;
            LLVMValueRef *mbrs = (LLVMValueRef *)malloc(
                (size_t)comps[c].n * sizeof(*mbrs));
            if (!mbrs) { sh_fail(&st, "out of memory"); ok = false; break; }
            for (int j = 0; j < comps[c].n; j++)
                mbrs[j] = mem[comps[c].idx[j]].fn;
            qsort(mbrs, (size_t)comps[c].n, sizeof(*mbrs), sh_cmp_name_ref);
            for (int j = 0; j < comps[c].n && ok; j++) {
                LLVMValueRef fn = mbrs[j];
                /* reference scan (fresh per shard so decl sets stay local) */
                {
                    sh_refs_t refs;
                    memset(&refs, 0, sizeof(refs));
                    refs.st = &st;
                    refs.closure_seen = &closure_memo;
                    sh_map_init(&refs.decl_fns);
                    sh_map_init(&refs.decl_globs);
                    sh_map_init(&refs.travel);
                    sh_map_init(&refs.body_done);
                    ok = sh_refs_scan_fn(&refs, fn);
                    for (int i = 0; i <= refs.decl_fns.mask; i++)
                        if (refs.decl_fns.vals[i] > 0)
                            sh_map_put(&decl_fns, refs.decl_fns.keys[i], 1);
                    for (int i = 0; i <= refs.decl_globs.mask; i++)
                        if (refs.decl_globs.vals[i] > 0)
                            sh_map_put(&decl_globs, refs.decl_globs.keys[i], 1);
                    for (int i = 0; i <= refs.travel.mask; i++)
                        if (refs.travel.vals[i] > 0)
                            sh_map_put(&travel, refs.travel.keys[i], 1);
                    sh_map_free(&refs.decl_fns);
                    sh_map_free(&refs.decl_globs);
                    sh_map_free(&refs.travel);
                    sh_map_free(&refs.body_done);
                    if (!ok) break;
                }
                char *fntxt = LLVMPrintValueToString(fn);
                if (!fntxt) {
                    sh_fail(&st, "body '%s' print failed", LLVMGetValueName(fn));
                    ok = false;
                    break;
                }
                /* comdat/alias can only appear on the define header line */
                {
                    const char *hnl = strchr(fntxt, '\n');
                    size_t hlen = hnl ? (size_t)(hnl - fntxt) : strlen(fntxt);
                    char hbuf[4096];
                    if (hlen >= sizeof(hbuf)) hlen = sizeof(hbuf) - 1;
                    memcpy(hbuf, fntxt, hlen);
                    hbuf[hlen] = '\0';
                    if (strstr(hbuf, " comdat($")) {
                        sh_fail(&st, "body '%s' carries comdat",
                                LLVMGetValueName(fn));
                        LLVMDisposeMessage(fntxt);
                        ok = false;
                        break;
                    }
                }
                sh_sb_puts(&bodies, fntxt);
                sh_sb_puts(&bodies, "\n");
                LLVMDisposeMessage(fntxt);
            }
            free(mbrs);
        }

        SH_TRACE("shard %d: bodies done (%d bytes)\n", s, (int)bodies.n);
        /* global lines. The decl/travel maps are pointer-keyed, so their
         * bucket order varies run to run with heap layout — collect and sort
         * by name, or the fragment (and thus the object file) is not
         * reproducible. */
        int ntrav = 0;
        for (int i = 0; i <= travel.mask; i++)
            if (travel.vals[i] > 0) ntrav++;
        if (ok && ntrav) {
            LLVMValueRef *tv = (LLVMValueRef *)malloc((size_t)ntrav * sizeof(*tv));
            if (!tv) { sh_fail(&st, "out of memory"); ok = false; }
            else {
                int t = 0;
                for (int i = 0; i <= travel.mask; i++)
                    if (travel.vals[i] > 0) tv[t++] = travel.keys[i];
                qsort(tv, (size_t)ntrav, sizeof(*tv), sh_cmp_name_ref);
                for (int t2 = 0; ok && t2 < ntrav; t2++) {
                    char *txt = LLVMPrintValueToString(tv[t2]);
                    if (!txt) { sh_fail(&st, "print global failed"); ok = false; break; }
                    if (strstr(txt, " comdat($") || strstr(txt, " = alias ")) {
                        sh_fail(&st, "global '%s' carries comdat/alias",
                                LLVMGetValueName(tv[t2]));
                        LLVMDisposeMessage(txt);
                        ok = false;
                        break;
                    }
                    sh_sb_puts(&gdecls, txt);
                    sh_sb_puts(&gdecls, "\n");
                    LLVMDisposeMessage(txt);
                }
                free(tv);
            }
        }
        int ngd = 0;
        for (int i = 0; i <= decl_globs.mask; i++)
            if (decl_globs.vals[i] > 0) ngd++;
        if (ok && ngd) {
            LLVMValueRef *gd = (LLVMValueRef *)malloc((size_t)ngd * sizeof(*gd));
            if (!gd) { sh_fail(&st, "out of memory"); ok = false; }
            else {
                int t = 0;
                for (int i = 0; i <= decl_globs.mask; i++)
                    if (decl_globs.vals[i] > 0) gd[t++] = decl_globs.keys[i];
                qsort(gd, (size_t)ngd, sizeof(*gd), sh_cmp_name_ref);
                for (int t2 = 0; t2 < ngd; t2++)
                    sh_emit_global_decl(&st, &gdecls, gd[t2]);
                free(gd);
            }
        }

        SH_TRACE("shard %d: globals done (%d bytes)\n", s, (int)gdecls.n);
        /* fn declares: everything referenced that is not a local member */
        if (ok) {
            /* union with needs_decl-marked other-shard members that this
             * body walk recorded */
            int nfd = 0;
            for (int i = 0; i <= decl_fns.mask; i++)
                if (decl_fns.vals[i] > 0 &&
                    sh_map_get(&local, decl_fns.keys[i]) < 0)
                    nfd++;
            if (nfd) {
                LLVMValueRef *fd = (LLVMValueRef *)malloc((size_t)nfd * sizeof(*fd));
                if (!fd) { sh_fail(&st, "out of memory"); ok = false; }
                else {
                    int t = 0;
                    for (int i = 0; i <= decl_fns.mask; i++)
                        if (decl_fns.vals[i] > 0 &&
                            sh_map_get(&local, decl_fns.keys[i]) < 0)
                            fd[t++] = decl_fns.keys[i];
                    qsort(fd, (size_t)nfd, sizeof(*fd), sh_cmp_name_ref);
                    for (int t2 = 0; t2 < nfd; t2++)
                        sh_emit_fn_decl(&gdecls, fd[t2]);
                    free(fd);
                }
            }
        }

        SH_TRACE("shard %d: declares done\n", s);
        /* types from all text so far */
        if (ok) {
            sh_harvest_types(&st, &types, bodies.p ? bodies.p : "");
            sh_harvest_types(&st, &types, gdecls.p ? gdecls.p : "");
        }

        /* final fragment */
        if (ok) {
            sh_sb_puts(&frag, types.p ? types.p : "");
            sh_sb_puts(&frag, gdecls.p ? gdecls.p : "");
            sh_sb_puts(&frag, bodies.p ? bodies.p : "");
            if (frag.oom || bodies.oom || gdecls.oom || types.oom) {
                sh_fail(&st, "out of memory assembling fragment");
                ok = false;
            }
        }

        if (ok) {
            char path[1200];
            snprintf(path, sizeof(path), "%s.shard%d.o", obj_base, s);
            char errbuf[256];
            char ptag[64];
            snprintf(ptag, sizeof(ptag), "shard %d: before emit", s);
            sh_probe_mem(ptag);
            if (sh_emit_one(&st, &frag, tm, path, errbuf, sizeof(errbuf))) {
                objs[s] = (char *)malloc(strlen(path) + 1);
                if (objs[s]) strcpy(objs[s], path);
                else sh_fail(&st, "out of memory");
                /* Instant eviction: this shard's machine code is now safely on disk.
                 * Clear all basic blocks of its functions in the coordinator module
                 * immediately, converting them to external declarations and reclaiming
                 * LLVM instruction objects per shard. */
                for (int c = 0; c < ncomp; c++) {
                    if (shard_of_comp[c] != s) continue;
                    for (int j = 0; j < comps[c].n; j++) {
                        LLVMValueRef fn = mem[comps[c].idx[j]].fn;
                        if (!LLVMIsDeclaration(fn)) {
                            LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
                            while (bb) {
                                LLVMBasicBlockRef next_bb = LLVMGetNextBasicBlock(bb);
                                LLVMDeleteBasicBlock(bb);
                                bb = next_bb;
                            }
                            LLVMSetLinkage(fn, LLVMExternalLinkage);
                        }
                    }
                }
            } else {
                sh_fail(&st, "shard %d: %s", s, errbuf);
                remove(path);
            }
            snprintf(ptag, sizeof(ptag), "shard %d: after emit", s);
            sh_probe_mem(ptag);
        }

        sh_map_free(&local);
        sh_map_free(&closure_memo);
        sh_map_free(&decl_fns); sh_map_free(&decl_globs); sh_map_free(&travel);
        sh_map_free(&st.type_done);
        st.type_done.keys = NULL; st.type_done.vals = NULL;
        free(bodies.p); free(gdecls.p); free(types.p); free(frag.p);
    }

    if (st.failed) {
        /* restore every linkage change, drop partial objects, fall back */
        for (int i = 0; i < st.link_n; i++)
            LLVMSetLinkage(st.link_recs[i].glob ? st.link_recs[i].glob
                                               : st.link_recs[i].fn,
                           st.link_recs[i].old_linkage);
        if (objs)
            for (int s = 0; s < nshard; s++) {
                if (objs[s]) { remove(objs[s]); free(objs[s]); }
            }
        free(objs);
        fprintf(stderr, "shard: %s — falling back to single module\n",
                st.reason);
        goto cleanup_planning;
    }

    /* ---- move: rename + external decl + RAUW for needs_decl members,
     * verify the coordinator, then bulk-delete the moved bodies ---- */
    {
        bool moved_ok = true;
        int rename_counter = 0;
        /* move records for rollback */
        sh_move_rec_t *moves = (sh_move_rec_t *)calloc((size_t)nm,
                                                        sizeof(*moves));
        int nmoves = 0;
        for (int k = 0; k < nm && moved_ok; k++) {
            if (mem[k].comp < 0) continue;
            if (sh_map_get(&st.needs_decl, mem[k].fn) < 0) continue;
            LLVMValueRef orig = mem[k].fn;
            /* LLVMGetValueName points into LLVM's own name storage: the
             * moment orig is renamed below, that storage is freed. Copy the
             * name out FIRST — every later read of the stale pointer sees
             * the new name instead (this exact UAF is what uniqued decls
             * into X.zsh$#N.NNNN before). */
            char oname[512];
            {
                const char *n = LLVMGetValueName(orig);
                size_t nl = n ? strlen(n) : 0;
                if (!n || nl == 0 || nl >= sizeof(oname)) {
                    sh_fail(&st, "member name too long or empty");
                    moved_ok = false; break;
                }
                memcpy(oname, n, nl + 1);
            }
            char decltmp[512], origtmp[512];
            if (!sh_unique_fn_name(g, oname, ".zsa$", &rename_counter,
                                   decltmp, sizeof(decltmp)) ||
                !sh_unique_fn_name(g, oname, ".zsb$", &rename_counter,
                                   origtmp, sizeof(origtmp))) {
                moved_ok = false; break;
            }
            /* No-window construction: the decl is born under a name verified
             * free, so LLVM cannot unique it behind our back (a uniqued decl
             * would carry X.zsa$#k.NNNN, phase C's clean-name lookup would
             * miss it, and the reference would dangle into link). The clean
             * name only moves onto the decl after the original provably
             * vacates it. */
            LLVMValueRef decl = LLVMAddFunction(
                g->mod, decltmp, LLVMGlobalGetValueType(orig));
            LLVMSetFunctionCallConv(decl, LLVMGetFunctionCallConv(orig));
            LLVMReplaceAllUsesWith(orig, decl);
            LLVMSetValueName2(orig, origtmp, strlen(origtmp));
            if (LLVMGetNamedFunction(g->mod, oname)) {
                sh_fail(&st, "clean name '%s' still occupied", oname);
                moved_ok = false; break;
            }
            LLVMSetValueName2(decl, oname, strlen(oname));
            moves[nmoves].orig = orig;
            moves[nmoves].decl = decl;
            moves[nmoves].origname = (char *)malloc(strlen(oname) + 1);
            if (moves[nmoves].origname) {
                strcpy(moves[nmoves].origname, oname);
                nmoves++;
            } else {
                moved_ok = false;
            }
        }
        if (moved_ok) {
            char *vmsg = NULL;
            if (LLVMVerifyModule(g->mod, LLVMReturnStatusAction, &vmsg)) {
                fprintf(stderr,
                        "shard: coordinator verify failed after move: %.160s\n",
                        vmsg ? vmsg : "?");
                moved_ok = false;
            }
            if (vmsg) LLVMDisposeMessage(vmsg);
        }
        if (!moved_ok) {
            /* rollback: point uses back at the originals, restore names and
             * linkage, drop the decls — the module returns to its pre-shard
             * state and the compile continues single-module */
            for (int i = 0; i < nmoves; i++) {
                LLVMReplaceAllUsesWith(moves[i].decl, moves[i].orig);
                LLVMDeleteFunction(moves[i].decl); /* frees the clean name */
                LLVMSetValueName2(moves[i].orig, moves[i].origname,
                                  strlen(moves[i].origname));
                free(moves[i].origname);
            }
            for (int i = 0; i < st.link_n; i++)
                LLVMSetLinkage(st.link_recs[i].glob ? st.link_recs[i].glob
                                                   : st.link_recs[i].fn,
                               st.link_recs[i].old_linkage);
            free(moves);
            fprintf(stderr, "shard: move rollback (%s) — single module\n",
                    st.failed ? st.reason : "verify failed");
            rc = 0;
            for (int s = 0; s < nshard; s++) {
                if (objs[s]) { remove(objs[s]); free(objs[s]); }
            }
            free(objs);
            goto cleanup_planning;
        }
        /* success: delete originals. A body with surviving references is only
         * deleted when the reference can be pointed at the external
         * declaration the shard object defines; otherwise the body STAYS in
         * the coordinator — the shard's internal copy is dead weight, while
         * synthesizing a declaration for an internal shard copy would
         * reference a symbol no object defines. */
        for (int k = 0; k < nm; k++) {
            if (mem[k].comp < 0) continue;
            LLVMValueRef orig = mem[k].fn;
            if (LLVMGetFirstUse(orig)) {
                if (sh_map_get(&st.needs_decl, orig) >= 0) {
                    /* phase A's decl carries the member's clean name and the
                     * shard exports it */
                    const char *on = LLVMGetValueName(orig);
                    char clean[512];
                    const char *cut = strstr(on, ".zsb$");
                    if (!cut) cut = strstr(on, ".zsh$");
                    if (!cut) cut = strstr(on, ".zshx$");
                    if (!cut) cut = on + strlen(on);
                    size_t len = (size_t)(cut - on);
                    if (len >= sizeof(clean)) len = sizeof(clean) - 1;
                    memcpy(clean, on, len);
                    clean[len] = '\0';
                    LLVMValueRef decl = LLVMGetNamedFunction(g->mod, clean);
                    if (decl && decl != orig && LLVMIsDeclaration(decl)) {
                        LLVMReplaceAllUsesWith(orig, decl);
                        if (!LLVMGetFirstUse(orig)) {
                            LLVMDeleteFunction(orig);
                            continue;
                        }
                    }
                }
                SH_TRACE("keeping '%s' in coordinator (surviving refs)\n",
                         LLVMGetValueName(orig));
                continue;
            }
            LLVMDeleteFunction(orig);
        }
        free(moves);
        sh_probe_mem("after delete all bodies");

        char *vmsg = NULL;
            if (LLVMVerifyModule(g->mod, LLVMReturnStatusAction, &vmsg)) {
                /* bodies are gone — cannot fall back; this is a bug */
                fprintf(stderr,
                        "error: shard: coordinator verify failed after delete: "
                        "%.160s\n", vmsg ? vmsg : "?");
                if (vmsg) LLVMDisposeMessage(vmsg);
                rc = -1;
            } else {
                if (sh_trace_on()) {
                    /* any leftover reference to a moved-and-renamed body
                     * would surface as an undefined symbol at link time */
                    for (LLVMValueRef f = LLVMGetFirstFunction(g->mod); f;
                         f = LLVMGetNextFunction(f)) {
                        if (LLVMIsDeclaration(f)) continue;
                        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(f);
                             bb; bb = LLVMGetNextBasicBlock(bb)) {
                            for (LLVMValueRef in = LLVMGetFirstInstruction(bb);
                                 in; in = LLVMGetNextInstruction(in)) {
                                unsigned nop = LLVMGetNumOperands(in);
                                for (unsigned k = 0; k < nop; k++) {
                                    LLVMValueRef op = LLVMGetOperand(in, (int)k);
                                    if (!op || !LLVMIsAFunction(op)) continue;
                                    const char *on = LLVMGetValueName(op);
                                    if (strstr(on, ".zsb$") || strstr(on, ".zsh$") ||
                                        strstr(on, ".zshx$"))
                                        SH_TRACE("post-delete ref: %s -> %s"
                                                 " [decl=%d lk=%d]\n",
                                                 LLVMGetValueName(f), on,
                                                 LLVMIsDeclaration(op),
                                                 (int)LLVMGetLinkage(op));
                                }
                            }
                        }
                    }
                    for (LLVMValueRef gv = LLVMGetFirstGlobal(g->mod); gv;
                         gv = LLVMGetNextGlobal(gv)) {
                        LLVMValueRef init = LLVMGetInitializer(gv);
                        if (!init) continue;
                        sh_trace_scan_const(gv, init, 0);
                    }
                }
            if (vmsg) LLVMDisposeMessage(vmsg);
            fprintf(stderr,
                    "shard: %d objects emitted, %d fns / %lld insns moved "
                    "(%.1f%% of module body)\n",
                    nshard, nm - dropped, mov_insns,
                    m->total_insns ? 100.0 * (double)mov_insns /
                                        (double)m->total_insns : 0.0);
            rc = nshard;
            *out_objs = objs;
            objs = NULL;
        }
    }

cleanup_planning:
    if (tm) LLVMDisposeTargetMachine(tm);
    for (int c = 0; c < ncomp; c++) free(comps[c].idx);
    free(comps);
    free(shard_of_comp);
    free(mem);
    sh_map_free(&mf2mem);
    free(st.link_recs);
    sh_map_free(&st.fn_v); sh_map_free(&st.glob_v);
    sh_map_free(&st.members_all);
    sh_map_free(&st.needs_decl);
    sh_map_free(&st.ext_globs);
    sh_map_free(&st.ext_fns);
    if (st.type_done.keys) sh_map_free(&st.type_done);
    return rc;
}
