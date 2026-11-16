/* ---- S4: deterministic immortal-constant blob (design.md §5) ----------------
 * publish+shard 下，SH_G_TRAVEL 判据把内部常量的完整定义复制进每个引用
 * 分片；托管字符串因此按分片放大 .rdata（f247 实测 69 片 2.31M 实例 vs
 * 30,971 唯一 payload；探针A：.rdata 44.96MB -> 8.82MB 单模块）。
 * 本模块在 shard 发射前把全模块的 immortal 托管字符串收口为单个确定性
 * blob + 常量偏移引用：
 *   - 收录两类字节映像（均为纯数据叶子，零重定位）：
 *       flat  [N x i8]  常量数据数组，前 8 字节为 RC sentinel 0xFF..FF
 *                       （__zan.strlit.* 托管串）；
 *       struct{i64,i64,[N x i8]} 且 field0 == -1（__zan.refl.str.pool.*）。
 *   - 唯一化按（映像字节序）排序后线性分配 8 对齐偏移，同输入必同布局；
 *   - blob 为 external 常量，定义留在协调器（主 object），分片经
 *     sh_refs_note_global 的 SH_G_DECL 路径自动获得 extern 声明；
 *   - 原字符串全局 RAUW 为
 *       getelementptr inbounds i8, ptr @__zan.const.blob, i64 0, <off>
 *     后删除；映像按原全局的完整字节布局（含头部与尾部对齐填充）入
 *     blob，使用点可见的内存内容逐字节不变（F6：immortal/unnamed_addr/
 *     只读 → 地址不显著，等值合并是既有事实）。
 * 失败语义：扫描阶段零变更；变异阶段先建全部常量、确认非空，再统一
 * RAUW+删除，无中途可失败点。任何后续失败回退单模块时，blob 形态仍是
 * 合法单模块（定义与引用同 object），行为不变。
 * 端序：shard 仅在原生 LE 目标启用（want_shard = native && !cross），
 * 头部字段按 LE 写出。 */

typedef struct {
    uint8_t  *img;      /* 完整字节映像（头部+payload+对齐填充） */
    size_t    len;      /* 映像长度 */
    long long offset;   /* blob 内偏移；-1 = 未分配 */
} sh_blob_ent_t;

typedef struct {
    sh_blob_ent_t *ents;  size_t n, cap;
    size_t        *slots; /* 槽位存 entry 下标 + 1；0 = 空 */
    size_t         mask, used;
} sh_blob_set_t;

typedef struct {
    LLVMValueRef gv;
    size_t       ent;    /* 所属 entry 下标 */
} sh_blob_ref_t;

static size_t sh_blob_hash(const uint8_t *p, size_t n) {
    size_t h = 2166136261U;
    for (size_t i = 0; i < n; i++) {
        h ^= (size_t)p[i];
        h *= 16777619U;
    }
    return h;
}

static bool sh_blob_set_init(sh_blob_set_t *s) {
    s->mask = 4095;
    s->used = 0;
    s->n = s->cap = 0;
    s->ents = NULL;
    s->slots = (size_t *)calloc(s->mask + 1, sizeof(*s->slots));
    return s->slots != NULL;
}

static void sh_blob_set_free(sh_blob_set_t *s) {
    for (size_t i = 0; i < s->n; i++) free(s->ents[i].img);
    free(s->ents);
    free(s->slots);
    memset(s, 0, sizeof(*s));
}

static void sh_blob_set_grow(sh_blob_set_t *s) {
    size_t ncap = (s->mask + 1) * 4;
    size_t *ns = (size_t *)calloc(ncap, sizeof(*ns));
    if (!ns) return; /* 保留旧表，插入会在满时失败 */
    for (size_t i = 0; i <= s->mask; i++) {
        if (!s->slots[i]) continue;
        size_t h = sh_blob_hash(s->ents[s->slots[i] - 1].img,
                                s->ents[s->slots[i] - 1].len) & (ncap - 1);
        while (ns[h]) h = (h + 1) & (ncap - 1);
        ns[h] = s->slots[i];
    }
    free(s->slots);
    s->slots = ns;
    s->mask = ncap - 1;
}

/* 返回 entry 下标；插入失败返回 SIZE_MAX */
static size_t sh_blob_set_intern(sh_blob_set_t *s, uint8_t *img, size_t len) {
    if ((s->used + 1) * 2 >= s->mask + 1) sh_blob_set_grow(s);
    size_t h = sh_blob_hash(img, len) & s->mask;
    while (s->slots[h]) {
        sh_blob_ent_t *e = &s->ents[s->slots[h] - 1];
        if (e->len == len && memcmp(e->img, img, len) == 0) {
            free(img);
            return s->slots[h] - 1;
        }
        h = (h + 1) & s->mask;
    }
    if (s->n == s->cap) {
        size_t ncap = s->cap ? s->cap * 2 : 256;
        sh_blob_ent_t *ne = (sh_blob_ent_t *)realloc(
            s->ents, ncap * sizeof(*ne));
        if (!ne) { free(img); return SIZE_MAX; }
        s->ents = ne;
        s->cap = ncap;
    }
    s->ents[s->n].img = img;
    s->ents[s->n].len = len;
    s->ents[s->n].offset = -1;
    s->slots[h] = s->n + 1;
    s->n++;
    s->used++;
    return s->n - 1;
}

static int sh_blob_cmp_ent(const void *a, const void *b) {
    const sh_blob_ent_t *x = *(const sh_blob_ent_t *const *)a;
    const sh_blob_ent_t *y = *(const sh_blob_ent_t *const *)b;
    if (x->len != y->len) return x->len < y->len ? -1 : 1;
    int c = memcmp(x->img, y->img, x->len);
    if (c) return c;
    return 0;
}

/* 抽取 i64 常量的无符号字节值（LE 写出）；非整型返回 false */
static bool sh_blob_i64_le(LLVMValueRef c, uint8_t out[8]) {
    if (!c || LLVMGetTypeKind(LLVMTypeOf(c)) != LLVMIntegerTypeKind)
        return false;
    if (LLVMGetIntTypeWidth(LLVMTypeOf(c)) != 64) return false;
    long long v = LLVMConstIntGetSExtValue(c);
    for (int i = 0; i < 8; i++) out[i] = (uint8_t)((unsigned long long)v >>
                                                   (i * 8));
    return true;
}

/* 平头形态：[N x i8] 常量数据数组，前 8 字节 = RC sentinel（全 FF）。
 * 必须先验 ConstantDataSequential：zeroinitializer（空串池项）非 CDS，
 * LLVMGetAsString 对其直接 cast 会崩（S4 首探即触雷）。 */
static bool sh_blob_flat_image(LLVMValueRef init, uint8_t **img, size_t *len) {
    if (!LLVMIsAConstantDataSequential(init)) return false;
    size_t n = 0;
    const char *raw = LLVMGetAsString(init, &n);
    if (!raw || n < 8) return false;
    for (int i = 0; i < 8; i++)
        if ((uint8_t)raw[i] != 0xFF) return false;
    uint8_t *buf = (uint8_t *)malloc(n);
    if (!buf) return false;
    memcpy(buf, raw, n);
    *img = buf;
    *len = n;
    return true;
}

/* 结构体形态：{i64, i64, [N x i8]} 且 field0 == -1（refl pool）。
 * 映像 = LE(field0) + LE(field1) + payload，补齐到 8 的倍数。 */
static bool sh_blob_struct_image(LLVMValueRef init, uint8_t **img,
                                 size_t *len) {
    LLVMTypeRef t = LLVMTypeOf(init);
    if (LLVMGetTypeKind(t) != LLVMStructTypeKind || LLVMIsPackedStruct(t))
        return false;
    if (LLVMCountStructElementTypes(t) != 3) return false;
    LLVMTypeRef f0t = LLVMStructGetTypeAtIndex(t, 0);
    LLVMTypeRef f1t = LLVMStructGetTypeAtIndex(t, 1);
    LLVMTypeRef f2t = LLVMStructGetTypeAtIndex(t, 2);
    if (!f0t || !f1t || !f2t) return false;
    if (LLVMGetTypeKind(f0t) != LLVMIntegerTypeKind ||
        LLVMGetIntTypeWidth(f0t) != 64 ||
        LLVMGetTypeKind(f1t) != LLVMIntegerTypeKind ||
        LLVMGetIntTypeWidth(f1t) != 64)
        return false;
    if (LLVMGetTypeKind(f2t) != LLVMArrayTypeKind) return false;
    LLVMTypeRef e2 = LLVMGetElementType(f2t);
    if (!e2 || LLVMGetTypeKind(e2) != LLVMIntegerTypeKind ||
        LLVMGetIntTypeWidth(e2) != 8)
        return false;
    LLVMValueRef v0 = LLVMGetAggregateElement(init, 0);
    LLVMValueRef v1 = LLVMGetAggregateElement(init, 1);
    LLVMValueRef v2 = LLVMGetAggregateElement(init, 2);
    uint8_t h0[8], h1[8];
    if (!sh_blob_i64_le(v0, h0) || !sh_blob_i64_le(v1, h1)) return false;
    /* immortal 判据：RC sentinel == -1 */
    static const uint8_t sentinel[8] = {0xFF, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0xFF, 0xFF, 0xFF};
    if (memcmp(h0, sentinel, 8) != 0) return false;
    size_t n = 0;
    if (!LLVMIsAConstantDataSequential(v2)) return false;
    const char *raw = LLVMGetAsString(v2, &n);
    if (!raw) return false;
    size_t stride = (16 + n + 7) & ~(size_t)7;
    uint8_t *buf = (uint8_t *)calloc(1, stride);
    if (!buf) return false;
    memcpy(buf, h0, 8);
    memcpy(buf + 8, h1, 8);
    memcpy(buf + 16, raw, n);
    *img = buf;
    *len = stride;
    return true;
}

/* 资格判定；命中时把映像 intern 进集合并返回下标 */
static size_t sh_blob_qualify(sh_state_t *st, sh_blob_set_t *set,
                              LLVMValueRef gv) {
    if (!LLVMIsAGlobalVariable(gv) || !LLVMIsGlobalConstant(gv))
        return SIZE_MAX;
    unsigned lk = LLVMGetLinkage(gv);
    if (lk != LLVMInternalLinkage && lk != LLVMPrivateLinkage) return SIZE_MAX;
    if (LLVMGetUnnamedAddress(gv) == LLVMNoUnnamedAddr) return SIZE_MAX;
    if (LLVMIsThreadLocal(gv)) return SIZE_MAX;
    if (LLVMGetVisibility(gv) != LLVMDefaultVisibility) return SIZE_MAX;
    if (LLVMGetDLLStorageClass(gv) != LLVMDefaultStorageClass) return SIZE_MAX;
    if (LLVMGetSection(gv)) return SIZE_MAX;
    if (LLVMHasMetadata(gv)) return SIZE_MAX;
    unsigned al = LLVMGetAlignment(gv);
    if (al != 0 && al != 1 && al != 8) return SIZE_MAX;
    LLVMValueRef init = LLVMGetInitializer(gv);
    if (!init || LLVMIsAConstantExpr(init)) return SIZE_MAX;
    uint8_t *img = NULL;
    size_t len = 0;
    if (!sh_blob_flat_image(init, &img, &len) &&
        !sh_blob_struct_image(init, &img, &len))
        return SIZE_MAX;
    (void)st;
    return sh_blob_set_intern(set, img, len);
}

/* 成功返回 true；失败已 sh_fail，模块保持未变异 */
static bool sh_build_const_blob(sh_state_t *st) {
    zan_irgen_t *g = st->g;
    sh_blob_set_t set;
    sh_blob_ref_t *refs = NULL;
    size_t nrefs = 0, refcap = 0;
    bool ok = sh_blob_set_init(&set);
    if (!ok) { sh_fail(st, "out of memory"); return false; }

    /* 阶段 1：零变更扫描（遍历次序 = 模块插入序，确定性） */
    SH_TRACE("blob: scan start\n");
    for (LLVMValueRef gv = LLVMGetFirstGlobal(g->mod); gv && ok;
         gv = LLVMGetNextGlobal(gv)) {
        size_t e = sh_blob_qualify(st, &set, gv);
        if (e == SIZE_MAX) {
            if (st->failed) ok = false;
            continue;
        }
        if (nrefs == refcap) {
            size_t ncap = refcap ? refcap * 2 : 256;
            sh_blob_ref_t *nr = (sh_blob_ref_t *)realloc(refs,
                ncap * sizeof(*nr));
            if (!nr) { sh_fail(st, "out of memory"); ok = false; break; }
            refs = nr;
            refcap = ncap;
        }
        refs[nrefs].gv = gv;
        refs[nrefs].ent = e;
        nrefs++;
    }
    if (!set.n || nrefs == 0) {
        /* 无可收口内容：干净跳过，不算失败（单模块回退路径不受影响） */
        free(refs);
        sh_blob_set_free(&set);
        return true;
    }
    long long total = 0;
    if (ok) {
        /* 阶段 2：确定性布局（字节序排序 → 8 对齐线性分配）。
         * 排序走下标置换（order[]），不得直接 qsort ents 本体：
         * refs[i].ent 存的是 entry 下标，本体置换会让引用错位到别的
         * entry（S4 首探即触雷：字段名指针指到别的字符串槽）。 */
        sh_blob_ent_t **order = (sh_blob_ent_t **)malloc(
            set.n * sizeof(*order));
        if (!order) { sh_fail(st, "out of memory"); ok = false; }
        if (ok) {
            for (size_t i = 0; i < set.n; i++) order[i] = &set.ents[i];
            qsort(order, set.n, sizeof(*order), sh_blob_cmp_ent);
            for (size_t k = 0; k < set.n; k++) {
                total = (total + 7) & ~7LL;
                order[k]->offset = total;
                total += (long long)order[k]->len;
            }
            free(order);
        }
        if (total <= 0 || total > (long long)1024 * 1024 * 1024) {
            sh_fail(st, "const blob size out of range (%lld)", total);
            ok = false;
        }
    }
    uint8_t *buf = NULL;
    LLVMValueRef cinit = NULL, blob = NULL;
    if (ok) {
        buf = (uint8_t *)calloc(1, (size_t)total);
        if (!buf) { sh_fail(st, "out of memory"); ok = false; }
    }
    if (ok) {
        SH_TRACE("blob: layout done (%d ents, %lld bytes), building init\n",
                 (int)set.n, total);
        for (size_t i = 0; i < set.n; i++)
            memcpy(buf + set.ents[i].offset, set.ents[i].img, set.ents[i].len);
        cinit = LLVMConstStringInContext2(g->ctx, (const char *)buf,
                                          (size_t)total, true);
        if (!cinit) { sh_fail(st, "out of memory"); ok = false; }
    }
    if (ok) {
        char name[64];
        for (int i = 0; ; i++) {
            snprintf(name, sizeof(name),
                     i ? "__zan.const.blob.%d" : "__zan.const.blob", i);
            if (!LLVMGetNamedGlobal(g->mod, name)) break;
        }
        LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
        blob = LLVMAddGlobal(g->mod, LLVMArrayType(i8t, (unsigned long long)total),
                             name);
        if (!blob) { sh_fail(st, "out of memory"); ok = false; }
    }
    if (ok) {
        /* blob 定义留在协调器（主 object）：external 常量使 sh_global_verdict
         * 判为 SH_G_DECL，分片只拿 extern 声明 —— 唯一定义片（§5 方案 A）。 */
        LLVMSetInitializer(blob, cinit);
        LLVMSetLinkage(blob, LLVMExternalLinkage);
        LLVMSetGlobalConstant(blob, true);
        LLVMSetAlignment(blob, 8);
    }
    /* 阶段 3：先全部折叠出常量 GEP，确认非空，再统一 RAUW+删除 */
    SH_TRACE("blob: gep fold for %d refs\n", (int)nrefs);
    LLVMValueRef *geps = NULL;
    if (ok) {
        geps = (LLVMValueRef *)malloc(nrefs * sizeof(*geps));
        if (!geps) { sh_fail(st, "out of memory"); ok = false; }
    }
    if (ok) {
        /* 偏移 GEP 必须用聚合形态（源元素类型 = blob 数组类型，{0,off} 即
         * 字节偏移）。标量 i8 源元素类型（getelementptr i8, ptr @g, 0, off）
         * 在 LLVM 20 常量折叠器里对带初始化器的全局会直接段错误
         * （_scratch/s1-thunk-c63/gep_repro.c 独立复现：off=0 折叠、off=24 崩），
         * 本仓既有常量 GEP（reflect_fields.c:67 / embedres_emit.c:95）均为
         * 聚合形态，非偶然。 */
        LLVMTypeRef bty = LLVMGlobalGetValueType(blob);
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        for (size_t i = 0; i < nrefs && ok; i++) {
            long long off = set.ents[refs[i].ent].offset;
            LLVMValueRef idx[2] = {
                LLVMConstInt(i64t, 0, false),
                LLVMConstInt(i64t, (unsigned long long)off, false)
            };
            geps[i] = LLVMConstInBoundsGEP2(bty, blob, idx, 2);
            if (!geps[i]) { sh_fail(st, "out of memory"); ok = false; }
        }
    }
    if (ok) {
        for (size_t i = 0; i < nrefs; i++) {
            LLVMReplaceAllUsesWith(refs[i].gv, geps[i]);
            LLVMDeleteGlobal(refs[i].gv);
        }
        fprintf(stderr,
                "shard: const blob: %d globals -> %d unique, %lld bytes\n",
                (int)nrefs, (int)set.n, total);
    }
    free(geps);
    free(buf);
    free(refs);
    sh_blob_set_free(&set);
    return ok;
}

/* ---- S4b: 内部常量全局一律不复制（design.md §8.3） --------------------------
 * 内部+常量全局（反射 fields/mtab/ctor/enum 表等）按初始化器打印文本
 * intern（键=内容全文，pool 语义：同键必同内容；同内容多份合并
 * RAUW+删除），canonical 升 external 留协调器为唯一定义；分片经既有
 * SH_G_DECL 路径（shard_emit.c sh_refs_note_global -> sh_link_glob）
 * 自动获得 extern 声明。含指针字段表不入 blob、逐全局 extern。
 * 前提核验：反射表只读、消费端仅按内容访问、无表指针身份契约（§8.3）；
 * 原名保留即确定性名（LLVM 全局名模块内唯一 + 唯一导出方=协调器）。
 *
 * 时序硬约束：必须先于 planning 的任何 sh_global_verdict 调用
 * （sh_planner_mark / mark_closure_fns / trial pass-1）——判据结果按
 * 全局 memo 在 st.glob_v，晋升后不失效，晚晋升会让 TRAVEL 旧判据残
 * 留、pass-2 仍整表切片（S4b 首探即触雷）。晋升只改 linkage，走
 * sh_link_rec 标准回滚链（trial 失败路径统一恢复），故可置于 trial
 * 闸门之前；blob 是破坏性重写（RAUW+删除不可回滚），必须留在 trial
 * 闸门之后——两者位置不可互换。 */
static bool sh_promote_const_globals(sh_state_t *st) {
    zan_irgen_t *g = st->g;
    sh_blob_set_t set;
    LLVMValueRef *canon = NULL;
    size_t ccap = 0;
    struct { LLVMValueRef gv, keep; } *dups = NULL;
    size_t ndups = 0, dupcap = 0;
    bool ok = sh_blob_set_init(&set);
    if (!ok) { sh_fail(st, "out of memory"); return false; }

    /* 阶段 1：零变更扫描 + 内容 intern（模块插入序，确定性） */
    SH_TRACE("promote: scan start\n");
    for (LLVMValueRef gv = LLVMGetFirstGlobal(g->mod); gv && ok;
         gv = LLVMGetNextGlobal(gv)) {
        if (LLVMIsDeclaration(gv) || LLVMHasMetadata(gv) ||
            LLVMIsThreadLocal(gv))
            continue;
        unsigned lk = LLVMGetLinkage(gv);
        if (lk != LLVMInternalLinkage && lk != LLVMPrivateLinkage) continue;
        if (!LLVMIsGlobalConstant(gv)) continue;
        LLVMValueRef init = LLVMGetInitializer(gv);
        if (!init) continue;
        /* blob 形态（strlit FF-sentinel / pool struct，含指针前哨兵的
         * 托管串）留给 sh_build_const_blob 在 trial 闸门之后打包——blob
         * 把全部串接进单一 external 定义，优于逐串晋升；此处跳过。 */
        size_t qe = sh_blob_qualify(st, &set, gv);
        if (st->failed) { ok = false; break; }
        if (qe != SIZE_MAX) continue;
        char *txt = LLVMPrintValueToString(init);
        if (!txt) { sh_fail(st, "out of memory"); ok = false; break; }
        size_t e = sh_blob_set_intern(&set, (uint8_t *)txt, strlen(txt));
        if (e == SIZE_MAX) { sh_fail(st, "out of memory"); ok = false; break; }
        if (set.n > ccap) {
            size_t ncap = set.n + 64;
            LLVMValueRef *nc = (LLVMValueRef *)realloc(
                canon, ncap * sizeof(*nc));
            if (!nc) { sh_fail(st, "out of memory"); ok = false; break; }
            memset(nc + ccap, 0, (ncap - ccap) * sizeof(*nc));
            canon = nc;
            ccap = ncap;
        }
        if (!canon[e]) {
            canon[e] = gv;
        } else {
            if (ndups == dupcap) {
                size_t ncap = dupcap ? dupcap * 2 : 64;
                void *nd = realloc(dups, ncap * sizeof(*dups));
                if (!nd) { sh_fail(st, "out of memory"); ok = false; break; }
                dups = nd;
                dupcap = ncap;
            }
            dups[ndups].gv = gv;
            dups[ndups].keep = canon[e];
            ndups++;
        }
    }

    /* 阶段 2：同内容合并（RAUW+删除；等值保持，无中途可失败点） */
    if (ok) {
        SH_TRACE("promote: merge %d dups\n", (int)ndups);
        for (size_t i = 0; i < ndups; i++) {
            LLVMReplaceAllUsesWith(dups[i].gv, dups[i].keep);
            LLVMDeleteGlobal(dups[i].gv);
        }
    }

    /* 阶段 3：canonical 升 external（原名保留；sh_link_rec 标准回滚） */
    size_t nprom = 0;
    if (ok) {
        SH_TRACE("promote: raise\n");
        for (size_t e = 0; e < set.n; e++) {
            if (!canon[e]) continue;
            nprom++;
            sh_link_rec(st, canon[e], true);
            LLVMSetLinkage(canon[e], LLVMExternalLinkage);
        }
    }
    if (ok)
        fprintf(stderr,
                "shard: const promote: %d promoted, %d merged\n",
                (int)nprom, (int)ndups);
    free(dups);
    free(canon);
    sh_blob_set_free(&set);
    return ok;
}
