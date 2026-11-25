/* irgen_builtins：按长度扫描的字符串助手（Split / Replace / 十进制解析）。
 *
 * 旧 Split/Replace 走 strstr + strlen：每次调用把整串与分隔符都 strlen 一遍，
 * 单字符分隔符要先物化成堆上的 1 字节串，切出的片段不记长度（之后每个
 * .Length 再 strlen）。这里统一改为以串头里已知的长度为界：
 *   - __zan_mem_find：memchr 定位首字节 + memcmp 校验余下字节（无 memmem
 *     的平台同样可用），支持串内含 \0；
 *   - __zan_str_split2：先数段再一次性预留 List 容量，每段一次分配、记长度；
 *   - __zan_str_replace2：先数命中再精确分配；等长替换直接整串拷贝后原位覆盖；
 *   - __zan_strtoll10：int.Parse/Convert.ToInt* 的十进制快路径，常见输入
 *     （可选 '-' + 1..18 位数字）不进 libc（strtoll 走 locale，实测占 numfmt
 *     ~45% 采样），其余（空白、'+'、超长、非数字开头）原样交给 strtoll，
 *     endp/errno 契约与 strtoll(s, endp, 10) 一致。 */

typedef struct {
    zan_irgen_t *g;
    LLVMValueRef fn;
    LLVMTypeRef i8, i8ptr, i32, i64;
} str_scan_ctx_t;

static str_scan_ctx_t str_scan_ctx(zan_irgen_t *g, LLVMValueRef fn) {
    str_scan_ctx_t c;
    c.g = g;
    c.fn = fn;
    c.i8 = LLVMInt8TypeInContext(g->ctx);
    c.i8ptr = LLVMPointerType(c.i8, 0);
    c.i32 = LLVMInt32TypeInContext(g->ctx);
    c.i64 = LLVMInt64TypeInContext(g->ctx);
    return c;
}

static LLVMBasicBlockRef ss_bb(str_scan_ctx_t *c, const char *name) {
    return LLVMAppendBasicBlockInContext(c->g->ctx, c->fn, name);
}

static LLVMValueRef ss_c64(str_scan_ctx_t *c, long long v) {
    return LLVMConstInt(c->i64, (unsigned long long)v, 1);
}

static LLVMValueRef ss_gep(str_scan_ctx_t *c, LLVMValueRef p, LLVMValueRef off, const char *name) {
    return LLVMBuildGEP2(c->g->builder, c->i8, p, &off, 1, name);
}

static LLVMValueRef ss_pdiff(str_scan_ctx_t *c, LLVMValueRef a, LLVMValueRef b, const char *name) {
    LLVMBuilderRef bd = c->g->builder;
    return LLVMBuildSub(bd, LLVMBuildPtrToInt(bd, a, c->i64, "pa"),
                        LLVMBuildPtrToInt(bd, b, c->i64, "pb"), name);
}

static void ss_memcpy(str_scan_ctx_t *c, LLVMValueRef d, LLVMValueRef s, LLVMValueRef n) {
    LLVMTypeRef ty = LLVMFunctionType(c->i8ptr, (LLVMTypeRef[]){c->i8ptr, c->i8ptr, c->i64}, 3, 0);
    zan_call2(c->g->builder, ty, get_libc_fn(c->g, "memcpy", ty), (LLVMValueRef[]){d, s, n}, 3, "");
}

/* 新串：分配 n+1、可选拷入 src[0..n)、补 \0、记长度。 */
static LLVMValueRef ss_new_str(str_scan_ctx_t *c, LLVMValueRef n, LLVMValueRef src) {
    LLVMBuilderRef bd = c->g->builder;
    LLVMValueRef buf = emit_string_alloc_rc(c->g, LLVMBuildAdd(bd, n, ss_c64(c, 1), "ns.sz"));
    if (src) ss_memcpy(c, buf, src, n);
    LLVMBuildStore(bd, LLVMConstInt(c->i8, 0, 0), ss_gep(c, buf, n, "ns.end"));
    emit_string_len_set(c->g, buf, n);
    return buf;
}

/* ptr __zan_memchr(ptr p, i32 c, i64 n)：同 memchr。短区间（典型的 CSV
 * 字段、键值片段、单词）逐字节扫——libc memchr 的调用 + SIMD 起步开销在
 * 十几字节的跨度上比扫描本身贵；长区间照旧交给 libc。 */
LLVMValueRef get_memchr_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_memchr");
    if (fn) return fn;
    str_scan_ctx_t c0 = str_scan_ctx(g, NULL);
    LLVMTypeRef fnty = LLVMFunctionType(c0.i8ptr, (LLVMTypeRef[]){c0.i8ptr, c0.i32, c0.i64}, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_memchr", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    str_scan_ctx_t c = str_scan_ctx(g, fn);
    LLVMBuilderRef bd = g->builder;
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(bd);
    LLVMBasicBlockRef entry = ss_bb(&c, "entry");
    LLVMBasicBlockRef small = ss_bb(&c, "small");
    LLVMBasicBlockRef body = ss_bb(&c, "body");
    LLVMBasicBlockRef next = ss_bb(&c, "next");
    LLVMBasicBlockRef found = ss_bb(&c, "found");
    LLVMBasicBlockRef big = ss_bb(&c, "big");
    LLVMBasicBlockRef miss = ss_bb(&c, "miss");
    LLVMValueRef p = LLVMGetParam(fn, 0), ch = LLVMGetParam(fn, 1);
    LLVMValueRef n = LLVMGetParam(fn, 2);
    LLVMPositionBuilderAtEnd(bd, entry);
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntULT, n, ss_c64(&c, 32), "short"), small, big);
    LLVMPositionBuilderAtEnd(bd, small);
    LLVMValueRef c8 = LLVMBuildTrunc(bd, ch, c.i8, "c8");
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntEQ, n, ss_c64(&c, 0), "empty"), miss, body);
    LLVMPositionBuilderAtEnd(bd, body);
    LLVMValueRef i = LLVMBuildPhi(bd, c.i64, "i");
    LLVMValueRef q = ss_gep(&c, p, i, "q");
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntEQ, LLVMBuildLoad2(bd, c.i8, q, "qc"), c8, "eq"),
                    found, next);
    LLVMPositionBuilderAtEnd(bd, next);
    LLVMValueRef i1 = LLVMBuildAdd(bd, i, ss_c64(&c, 1), "i1");
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntULT, i1, n, "more"), body, miss);
    LLVMAddIncoming(i, (LLVMValueRef[]){ss_c64(&c, 0), i1}, (LLVMBasicBlockRef[]){small, next}, 2);
    LLVMPositionBuilderAtEnd(bd, found);
    LLVMBuildRet(bd, q);
    LLVMPositionBuilderAtEnd(bd, big);
    LLVMTypeRef memchr_ty = LLVMFunctionType(c.i8ptr, (LLVMTypeRef[]){c.i8ptr, c.i32, c.i64}, 3, 0);
    LLVMBuildRet(bd, zan_call2(bd, memchr_ty, get_libc_fn(g, "memchr", memchr_ty),
                               (LLVMValueRef[]){p, ch, n}, 3, "lc"));
    LLVMPositionBuilderAtEnd(bd, miss);
    LLVMBuildRet(bd, LLVMConstNull(c.i8ptr));
    if (saved) LLVMPositionBuilderAtEnd(bd, saved);
    return fn;
}

/* ptr __zan_mem_find(ptr hay, i64 hlen, ptr needle, i64 nlen)：nlen >= 1；
 * 返回首个命中位置或 null。 */
static LLVMValueRef get_mem_find_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_mem_find");
    if (fn) return fn;
    str_scan_ctx_t c0 = str_scan_ctx(g, NULL);
    LLVMTypeRef fnty =
        LLVMFunctionType(c0.i8ptr, (LLVMTypeRef[]){c0.i8ptr, c0.i64, c0.i8ptr, c0.i64}, 4, 0);
    fn = LLVMAddFunction(g->mod, "__zan_mem_find", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    str_scan_ctx_t c = str_scan_ctx(g, fn);
    LLVMBuilderRef bd = g->builder;
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(bd);
    LLVMBasicBlockRef entry = ss_bb(&c, "entry");
    LLVMBasicBlockRef setup = ss_bb(&c, "setup");
    LLVMBasicBlockRef loop = ss_bb(&c, "loop");
    LLVMBasicBlockRef check = ss_bb(&c, "check");
    LLVMBasicBlockRef cmp = ss_bb(&c, "cmp");
    LLVMBasicBlockRef nomatch = ss_bb(&c, "nomatch");
    LLVMBasicBlockRef found = ss_bb(&c, "found");
    LLVMBasicBlockRef miss = ss_bb(&c, "miss");
    LLVMValueRef h = LLVMGetParam(fn, 0), hlen = LLVMGetParam(fn, 1);
    LLVMValueRef nd = LLVMGetParam(fn, 2), nlen = LLVMGetParam(fn, 3);

    LLVMPositionBuilderAtEnd(bd, entry);
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntUGT, nlen, hlen, "toolong"), miss, setup);

    LLVMPositionBuilderAtEnd(bd, setup);
    LLVMValueRef span = LLVMBuildAdd(bd, LLVMBuildSub(bd, hlen, nlen, "hn"), ss_c64(&c, 1), "span");
    LLVMValueRef lim = ss_gep(&c, h, span, "lim");
    LLVMValueRef n0 = LLVMBuildZExt(bd, LLVMBuildLoad2(bd, c.i8, nd, "n0"), c.i32, "n0i");
    LLVMValueRef rest = LLVMBuildSub(bd, nlen, ss_c64(&c, 1), "rest");
    LLVMValueRef n1 = ss_gep(&c, nd, ss_c64(&c, 1), "n1");
    LLVMBuildBr(bd, loop);

    LLVMPositionBuilderAtEnd(bd, loop);
    LLVMValueRef cur = LLVMBuildPhi(bd, c.i8ptr, "cur");
    LLVMValueRef mc = get_memchr_fn(g);
    LLVMValueRef hit =
        zan_call2(bd, LLVMGlobalGetValueType(mc), mc,
                  (LLVMValueRef[]){cur, n0, ss_pdiff(&c, lim, cur, "avail")}, 3, "hit");
    LLVMBuildCondBr(bd, LLVMBuildIsNull(bd, hit, "hnull"), miss, check);

    LLVMPositionBuilderAtEnd(bd, check);
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntEQ, rest, ss_c64(&c, 0), "one"), found, cmp);

    LLVMPositionBuilderAtEnd(bd, cmp);
    LLVMTypeRef memcmp_ty = LLVMFunctionType(c.i32, (LLVMTypeRef[]){c.i8ptr, c.i8ptr, c.i64}, 3, 0);
    LLVMValueRef r =
        zan_call2(bd, memcmp_ty, get_libc_fn(g, "memcmp", memcmp_ty),
                  (LLVMValueRef[]){ss_gep(&c, hit, ss_c64(&c, 1), "h1"), n1, rest}, 3, "mc");
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntEQ, r, LLVMConstInt(c.i32, 0, 0), "eq"), found,
                    nomatch);

    LLVMPositionBuilderAtEnd(bd, nomatch);
    LLVMValueRef nx = ss_gep(&c, hit, ss_c64(&c, 1), "nx");
    LLVMBuildCondBr(
        bd, LLVMBuildICmp(bd, LLVMIntSGT, ss_pdiff(&c, lim, nx, "left"), ss_c64(&c, 0), "more"),
        loop, miss);
    LLVMAddIncoming(cur, (LLVMValueRef[]){h, nx}, (LLVMBasicBlockRef[]){setup, nomatch}, 2);

    LLVMPositionBuilderAtEnd(bd, found);
    LLVMBuildRet(bd, hit);
    LLVMPositionBuilderAtEnd(bd, miss);
    LLVMBuildRet(bd, LLVMConstNull(c.i8ptr));
    if (saved) LLVMPositionBuilderAtEnd(bd, saved);
    return fn;
}

static LLVMValueRef ss_find(str_scan_ctx_t *c, LLVMValueRef cur, LLVMValueRef end, LLVMValueRef nd,
                            LLVMValueRef nlen, const char *name) {
    LLVMValueRef ff = get_mem_find_fn(c->g);
    return zan_call2(c->g->builder, LLVMGlobalGetValueType(ff), ff,
                     (LLVMValueRef[]){cur, ss_pdiff(c, end, cur, "hl"), nd, nlen}, 4, name);
}

/* 数 [s, end) 中 needle 的不重叠命中次数（nlen >= 1）。 */
static LLVMValueRef ss_count(str_scan_ctx_t *c, LLVMValueRef s, LLVMValueRef end, LLVMValueRef nd,
                             LLVMValueRef nlen) {
    LLVMBuilderRef bd = c->g->builder;
    LLVMBasicBlockRef pre = LLVMGetInsertBlock(bd);
    LLVMBasicBlockRef loop = ss_bb(c, "cnt.loop");
    LLVMBasicBlockRef inc = ss_bb(c, "cnt.inc");
    LLVMBasicBlockRef done = ss_bb(c, "cnt.done");
    LLVMBuildBr(bd, loop);
    LLVMPositionBuilderAtEnd(bd, loop);
    LLVMValueRef cur = LLVMBuildPhi(bd, c->i8ptr, "cnt.cur");
    LLVMValueRef n = LLVMBuildPhi(bd, c->i64, "cnt.n");
    LLVMValueRef hit = ss_find(c, cur, end, nd, nlen, "cnt.hit");
    LLVMBuildCondBr(bd, LLVMBuildIsNull(bd, hit, "cnt.miss"), done, inc);
    LLVMPositionBuilderAtEnd(bd, inc);
    LLVMValueRef n1 = LLVMBuildAdd(bd, n, ss_c64(c, 1), "cnt.n1");
    LLVMValueRef nx = ss_gep(c, hit, nlen, "cnt.nx");
    LLVMBuildBr(bd, loop);
    LLVMAddIncoming(cur, (LLVMValueRef[]){s, nx}, (LLVMBasicBlockRef[]){pre, inc}, 2);
    LLVMAddIncoming(n, (LLVMValueRef[]){ss_c64(c, 0), n1}, (LLVMBasicBlockRef[]){pre, inc}, 2);
    LLVMPositionBuilderAtEnd(bd, done);
    return n;
}

/* C 可见长度：min(头长度, 首个 \0 的位置)。搜索/变换类方法（Split、Replace）
 * 与 C 宿主、自举后端同口径地停在首个 \0（钉子 string_nul_search.zan）。 */
static LLVMValueRef ss_clen(str_scan_ctx_t *c, LLVMValueRef p, LLVMValueRef len, const char *name) {
    LLVMBuilderRef bd = c->g->builder;
    LLVMValueRef mc = get_memchr_fn(c->g);
    LLVMValueRef z = zan_call2(bd, LLVMGlobalGetValueType(mc), mc,
                               (LLVMValueRef[]){p, LLVMConstInt(c->i32, 0, 0), len}, 3, "nul");
    return LLVMBuildSelect(bd, LLVMBuildIsNull(bd, z, "nonul"), len, ss_pdiff(c, z, p, "nulat"),
                           name);
}

/* ptr __zan_str_replace2(ptr s, i64 ls, ptr from, i64 lf, ptr to, i64 lt) */
LLVMValueRef get_str_replace_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_replace2");
    if (fn) return fn;
    str_scan_ctx_t c0 = str_scan_ctx(g, NULL);
    LLVMTypeRef fnty = LLVMFunctionType(
        c0.i8ptr, (LLVMTypeRef[]){c0.i8ptr, c0.i64, c0.i8ptr, c0.i64, c0.i8ptr, c0.i64}, 6, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_replace2", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    str_scan_ctx_t c = str_scan_ctx(g, fn);
    LLVMBuilderRef bd = g->builder;
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(bd);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;
    LLVMBasicBlockRef entry = ss_bb(&c, "entry");
    LLVMBasicBlockRef dup = ss_bb(&c, "dup");
    LLVMBasicBlockRef chk = ss_bb(&c, "chk");
    LLVMBasicBlockRef same = ss_bb(&c, "same");
    LLVMBasicBlockRef grow = ss_bb(&c, "grow");
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef from = LLVMGetParam(fn, 2);
    LLVMValueRef to = LLVMGetParam(fn, 4);

    LLVMPositionBuilderAtEnd(bd, entry);
    LLVMValueRef ls = ss_clen(&c, s, LLVMGetParam(fn, 1), "ls");
    LLVMValueRef lf = ss_clen(&c, from, LLVMGetParam(fn, 3), "lf");
    LLVMValueRef lt = ss_clen(&c, to, LLVMGetParam(fn, 5), "lt");
    LLVMValueRef end = ss_gep(&c, s, ls, "end");
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntEQ, lf, ss_c64(&c, 0), "fempty"), dup, chk);

    LLVMPositionBuilderAtEnd(bd, dup);
    LLVMBuildRet(bd, ss_new_str(&c, ls, s));

    LLVMPositionBuilderAtEnd(bd, chk);
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntEQ, lf, lt, "samelen"), same, grow);

    /* 等长：整串拷贝，再把每个命中原位覆盖成 to——单遍、无计数。 */
    {
        LLVMPositionBuilderAtEnd(bd, same);
        LLVMValueRef buf = ss_new_str(&c, ls, s);
        LLVMBasicBlockRef pre = LLVMGetInsertBlock(bd);
        LLVMBasicBlockRef loop = ss_bb(&c, "eq.loop");
        LLVMBasicBlockRef put = ss_bb(&c, "eq.put");
        LLVMBasicBlockRef done = ss_bb(&c, "eq.done");
        LLVMBuildBr(bd, loop);
        LLVMPositionBuilderAtEnd(bd, loop);
        LLVMValueRef cur = LLVMBuildPhi(bd, c.i8ptr, "eq.cur");
        LLVMValueRef hit = ss_find(&c, cur, end, from, lf, "eq.hit");
        LLVMBuildCondBr(bd, LLVMBuildIsNull(bd, hit, "eq.miss"), done, put);
        LLVMPositionBuilderAtEnd(bd, put);
        ss_memcpy(&c, ss_gep(&c, buf, ss_pdiff(&c, hit, s, "eq.off"), "eq.dp"), to, lt);
        LLVMValueRef nx = ss_gep(&c, hit, lf, "eq.nx");
        LLVMBuildBr(bd, loop);
        LLVMAddIncoming(cur, (LLVMValueRef[]){s, nx}, (LLVMBasicBlockRef[]){pre, put}, 2);
        LLVMPositionBuilderAtEnd(bd, done);
        LLVMBuildRet(bd, buf);
    }

    /* 变长：数命中 → 精确分配 → 逐段拷贝。 */
    {
        LLVMPositionBuilderAtEnd(bd, grow);
        LLVMValueRef n = ss_count(&c, s, end, from, lf);
        LLVMValueRef newlen = LLVMBuildAdd(
            bd, ls, LLVMBuildMul(bd, n, LLVMBuildSub(bd, lt, lf, "delta"), "dl"), "newlen");
        LLVMValueRef buf = emit_string_alloc_rc(g, LLVMBuildAdd(bd, newlen, ss_c64(&c, 1), "bsz"));
        LLVMBasicBlockRef pre = LLVMGetInsertBlock(bd);
        LLVMBasicBlockRef loop = ss_bb(&c, "bl.loop");
        LLVMBasicBlockRef put = ss_bb(&c, "bl.put");
        LLVMBasicBlockRef tail = ss_bb(&c, "bl.tail");
        LLVMBuildBr(bd, loop);
        LLVMPositionBuilderAtEnd(bd, loop);
        LLVMValueRef cur = LLVMBuildPhi(bd, c.i8ptr, "bl.cur");
        LLVMValueRef d = LLVMBuildPhi(bd, c.i8ptr, "bl.d");
        LLVMValueRef hit = ss_find(&c, cur, end, from, lf, "bl.hit");
        LLVMBuildCondBr(bd, LLVMBuildIsNull(bd, hit, "bl.miss"), tail, put);
        LLVMPositionBuilderAtEnd(bd, put);
        LLVMValueRef seg = ss_pdiff(&c, hit, cur, "bl.seg");
        ss_memcpy(&c, d, cur, seg);
        LLVMValueRef d1 = ss_gep(&c, d, seg, "bl.d1");
        ss_memcpy(&c, d1, to, lt);
        LLVMValueRef d2 = ss_gep(&c, d1, lt, "bl.d2");
        LLVMValueRef nx = ss_gep(&c, hit, lf, "bl.nx");
        LLVMBuildBr(bd, loop);
        LLVMAddIncoming(cur, (LLVMValueRef[]){s, nx}, (LLVMBasicBlockRef[]){pre, put}, 2);
        LLVMAddIncoming(d, (LLVMValueRef[]){buf, d2}, (LLVMBasicBlockRef[]){pre, put}, 2);
        LLVMPositionBuilderAtEnd(bd, tail);
        LLVMValueRef rest = ss_pdiff(&c, end, cur, "bl.rest");
        ss_memcpy(&c, d, cur, rest);
        LLVMBuildStore(bd, LLVMConstInt(c.i8, 0, 0), ss_gep(&c, d, rest, "bl.endp"));
        emit_string_len_set(g, buf, newlen);
        LLVMBuildRet(bd, buf);
    }
    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(bd, saved);
    return fn;
}

/* void __zan_str_split2(ptr s, i64 slen, ptr sep, i64 seplen, ptr list,
 *                       i64 opt)：opt bit0 = RemoveEmptyEntries。 */
LLVMValueRef get_str_split_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_split2");
    if (fn) return fn;
    str_scan_ctx_t c0 = str_scan_ctx(g, NULL);
    LLVMTypeRef fnty = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){c0.i8ptr, c0.i64, c0.i8ptr, c0.i64, c0.i8ptr, c0.i64}, 6, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_split2", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    str_scan_ctx_t c = str_scan_ctx(g, fn);
    LLVMBuilderRef bd = g->builder;
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(bd);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;
    LLVMValueRef push_fn = get_list_push_strn_fn(g);
    LLVMTypeRef push_ty = LLVMGlobalGetValueType(push_fn);
    LLVMBasicBlockRef entry = ss_bb(&c, "entry");
    LLVMBasicBlockRef scan = ss_bb(&c, "scan");
    LLVMBasicBlockRef grow = ss_bb(&c, "grow");
    LLVMBasicBlockRef run = ss_bb(&c, "run");
    LLVMBasicBlockRef loop = ss_bb(&c, "loop");
    LLVMBasicBlockRef hitbb = ss_bb(&c, "hit");
    LLVMBasicBlockRef push = ss_bb(&c, "push");
    LLVMBasicBlockRef next = ss_bb(&c, "next");
    LLVMBasicBlockRef last = ss_bb(&c, "last");
    LLVMBasicBlockRef push_last = ss_bb(&c, "push_last");
    LLVMBasicBlockRef done = ss_bb(&c, "done");
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef sep = LLVMGetParam(fn, 2);
    LLVMValueRef lst = LLVMGetParam(fn, 4), opt = LLVMGetParam(fn, 5);

    LLVMPositionBuilderAtEnd(bd, entry);
    LLVMValueRef slen = ss_clen(&c, s, LLVMGetParam(fn, 1), "slen");
    LLVMValueRef seplen = ss_clen(&c, sep, LLVMGetParam(fn, 3), "seplen");
    LLVMValueRef end = ss_gep(&c, s, slen, "end");
    LLVMValueRef rm_empty = LLVMBuildTrunc(bd, opt, LLVMInt1TypeInContext(g->ctx), "rm_empty");
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntEQ, seplen, ss_c64(&c, 0), "sempty"), last, scan);

    /* 先数段，一次把 List 容量预留够：免去 0→4→8… 的 realloc 链。 */
    LLVMPositionBuilderAtEnd(bd, scan);
    LLVMValueRef nseg;
    {
        /* 单字节分隔符：逐字节计数（LLVM 会向量化）；多字节走查找循环。 */
        LLVMBasicBlockRef one = ss_bb(&c, "cnt1");
        LLVMBasicBlockRef body1 = ss_bb(&c, "cnt1.body");
        LLVMBasicBlockRef multi = ss_bb(&c, "cntn");
        LLVMBasicBlockRef joined = ss_bb(&c, "cnt.join");
        LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntEQ, seplen, ss_c64(&c, 1), "sep1"), one,
                        multi);
        LLVMPositionBuilderAtEnd(bd, one);
        LLVMValueRef sc = LLVMBuildLoad2(bd, c.i8, sep, "sc");
        LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntEQ, slen, ss_c64(&c, 0), "s0"), joined, body1);
        LLVMPositionBuilderAtEnd(bd, body1);
        LLVMValueRef i = LLVMBuildPhi(bd, c.i64, "ci");
        LLVMValueRef k = LLVMBuildPhi(bd, c.i64, "ck");
        LLVMValueRef bc = LLVMBuildLoad2(bd, c.i8, ss_gep(&c, s, i, "cp"), "bc");
        LLVMValueRef k1 = LLVMBuildAdd(
            bd, k, LLVMBuildZExt(bd, LLVMBuildICmp(bd, LLVMIntEQ, bc, sc, "isep"), c.i64, "isepz"),
            "k1");
        LLVMValueRef i1 = LLVMBuildAdd(bd, i, ss_c64(&c, 1), "ci1");
        LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntULT, i1, slen, "cmore"), body1, joined);
        LLVMAddIncoming(i, (LLVMValueRef[]){ss_c64(&c, 0), i1}, (LLVMBasicBlockRef[]){one, body1},
                        2);
        LLVMAddIncoming(k, (LLVMValueRef[]){ss_c64(&c, 0), k1}, (LLVMBasicBlockRef[]){one, body1},
                        2);
        LLVMPositionBuilderAtEnd(bd, multi);
        LLVMValueRef kn = ss_count(&c, s, end, sep, seplen);
        LLVMBasicBlockRef multi_end = LLVMGetInsertBlock(bd);
        LLVMBuildBr(bd, joined);
        LLVMPositionBuilderAtEnd(bd, joined);
        LLVMValueRef kk = LLVMBuildPhi(bd, c.i64, "nsep");
        LLVMAddIncoming(kk, (LLVMValueRef[]){ss_c64(&c, 0), k1, kn},
                        (LLVMBasicBlockRef[]){one, body1, multi_end}, 3);
        nseg = LLVMBuildAdd(bd, kk, ss_c64(&c, 1), "nseg");
    }
    LLVMValueRef lp = LLVMBuildBitCast(bd, lst, LLVMPointerType(g->list_struct_type, 0), "lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(bd, g->list_struct_type, lp, 0, "cntp");
    LLVMValueRef capp = LLVMBuildStructGEP2(bd, g->list_struct_type, lp, 1, "capp");
    LLVMValueRef datap = LLVMBuildStructGEP2(bd, g->list_struct_type, lp, 2, "datap");
    LLVMValueRef need = LLVMBuildAdd(bd, LLVMBuildLoad2(bd, c.i64, cntp, "cnt"), nseg, "need");
    LLVMValueRef cap = LLVMBuildLoad2(bd, c.i64, capp, "cap");
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntSGT, need, cap, "short"), grow, run);

    LLVMPositionBuilderAtEnd(bd, grow);
    {
        LLVMValueRef odata = LLVMBuildLoad2(bd, c.i8ptr, datap, "odata");
        LLVMValueRef nd = zan_call2(
            bd, LLVMFunctionType(c.i8ptr, (LLVMTypeRef[]){c.i8ptr, c.i64}, 2, 0), g->fn_realloc,
            (LLVMValueRef[]){odata, LLVMBuildMul(bd, need, ss_c64(&c, 8), "nb")}, 2, "ndata");
        zan_irgen_emit_oom_check(g, fn, nd);
        LLVMBuildStore(bd, nd, datap);
        LLVMBuildStore(bd, need, capp);
        LLVMBuildBr(bd, run);
    }

    LLVMPositionBuilderAtEnd(bd, run);
    LLVMBasicBlockRef run_end = LLVMGetInsertBlock(bd);
    LLVMBuildBr(bd, loop);

    LLVMPositionBuilderAtEnd(bd, loop);
    LLVMValueRef cur = LLVMBuildPhi(bd, c.i8ptr, "cur");
    LLVMValueRef hit = ss_find(&c, cur, end, sep, seplen, "hit");
    LLVMBuildCondBr(bd, LLVMBuildIsNull(bd, hit, "miss"), last, hitbb);

    LLVMPositionBuilderAtEnd(bd, hitbb);
    LLVMValueRef seg = ss_pdiff(&c, hit, cur, "seg");
    LLVMBuildCondBr(
        bd,
        LLVMBuildAnd(bd, rm_empty, LLVMBuildICmp(bd, LLVMIntEQ, seg, ss_c64(&c, 0), "z"), "skip"),
        next, push);
    LLVMPositionBuilderAtEnd(bd, push);
    zan_call2(bd, push_ty, push_fn, (LLVMValueRef[]){lst, cur, seg}, 3, "");
    LLVMBuildBr(bd, next);
    LLVMPositionBuilderAtEnd(bd, next);
    LLVMValueRef nx = ss_gep(&c, hit, seplen, "nx");
    LLVMBuildBr(bd, loop);
    LLVMAddIncoming(cur, (LLVMValueRef[]){s, nx}, (LLVMBasicBlockRef[]){run_end, next}, 2);

    LLVMPositionBuilderAtEnd(bd, last);
    LLVMValueRef tp = LLVMBuildPhi(bd, c.i8ptr, "tp");
    LLVMAddIncoming(tp, (LLVMValueRef[]){s, cur}, (LLVMBasicBlockRef[]){entry, loop}, 2);
    LLVMValueRef rest = ss_pdiff(&c, end, tp, "rest");
    LLVMBuildCondBr(bd,
                    LLVMBuildAnd(bd, rm_empty,
                                 LLVMBuildICmp(bd, LLVMIntEQ, rest, ss_c64(&c, 0), "rz"),
                                 "skip_last"),
                    done, push_last);
    LLVMPositionBuilderAtEnd(bd, push_last);
    zan_call2(bd, push_ty, push_fn, (LLVMValueRef[]){lst, tp, rest}, 3, "");
    LLVMBuildBr(bd, done);
    LLVMPositionBuilderAtEnd(bd, done);
    LLVMBuildRetVoid(bd);
    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(bd, saved);
    return fn;
}

/* i64 __zan_strtoll10(ptr s, ptr endp, i32 base)：与 strtoll 同型同契约。 */
LLVMValueRef get_strtoll10_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_strtoll10");
    if (fn) return fn;
    str_scan_ctx_t c0 = str_scan_ctx(g, NULL);
    LLVMTypeRef i8pp = LLVMPointerType(c0.i8ptr, 0);
    LLVMTypeRef fnty = LLVMFunctionType(c0.i64, (LLVMTypeRef[]){c0.i8ptr, i8pp, c0.i32}, 3, 0);
    LLVMValueRef libc = get_libc_fn(g, "strtoll", fnty);
    fn = LLVMAddFunction(g->mod, "__zan_strtoll10", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    str_scan_ctx_t c = str_scan_ctx(g, fn);
    LLVMBuilderRef bd = g->builder;
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(bd);
    LLVMBasicBlockRef entry = ss_bb(&c, "entry");
    LLVMBasicBlockRef loop = ss_bb(&c, "loop");
    LLVMBasicBlockRef body = ss_bb(&c, "body");
    LLVMBasicBlockRef capped = ss_bb(&c, "capped");
    LLVMBasicBlockRef stop = ss_bb(&c, "stop");
    LLVMBasicBlockRef fast = ss_bb(&c, "fast");
    LLVMBasicBlockRef slow = ss_bb(&c, "slow");
    LLVMValueRef s = LLVMGetParam(fn, 0), endp = LLVMGetParam(fn, 1);
    LLVMValueRef base = LLVMGetParam(fn, 2);

    LLVMPositionBuilderAtEnd(bd, entry);
    LLVMValueRef c0v = LLVMBuildLoad2(bd, c.i8, s, "c0");
    LLVMValueRef neg = LLVMBuildICmp(bd, LLVMIntEQ, c0v, LLVMConstInt(c.i8, '-', 0), "neg");
    LLVMValueRef p0 = LLVMBuildSelect(bd, neg, ss_gep(&c, s, ss_c64(&c, 1), "s1"), s, "p0");
    LLVMValueRef is10 = LLVMBuildICmp(bd, LLVMIntEQ, base, LLVMConstInt(c.i32, 10, 0), "is10");
    LLVMBuildCondBr(bd, is10, loop, slow);

    LLVMPositionBuilderAtEnd(bd, loop);
    LLVMValueRef i = LLVMBuildPhi(bd, c.i64, "i");
    LLVMValueRef acc = LLVMBuildPhi(bd, c.i64, "acc");
    LLVMValueRef ch = LLVMBuildLoad2(bd, c.i8, ss_gep(&c, p0, i, "cp"), "ch");
    LLVMValueRef d = LLVMBuildSub(bd, LLVMBuildZExt(bd, ch, c.i64, "chz"), ss_c64(&c, '0'), "d");
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntULT, d, ss_c64(&c, 10), "isd"), body, stop);

    LLVMPositionBuilderAtEnd(bd, body);
    LLVMValueRef acc1 = LLVMBuildAdd(bd, LLVMBuildMul(bd, acc, ss_c64(&c, 10), "acc10"), d, "acc1");
    LLVMValueRef i1 = LLVMBuildAdd(bd, i, ss_c64(&c, 1), "i1");
    /* 18 位以内 i64 不会溢出；第 19 位起交给 strtoll 判 ERANGE。 */
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntEQ, i1, ss_c64(&c, 18), "cap"), capped, loop);
    LLVMAddIncoming(i, (LLVMValueRef[]){ss_c64(&c, 0), i1}, (LLVMBasicBlockRef[]){entry, body}, 2);
    LLVMAddIncoming(acc, (LLVMValueRef[]){ss_c64(&c, 0), acc1}, (LLVMBasicBlockRef[]){entry, body},
                    2);

    LLVMPositionBuilderAtEnd(bd, capped);
    LLVMValueRef c18 = LLVMBuildLoad2(bd, c.i8, ss_gep(&c, p0, ss_c64(&c, 18), "p18"), "c18");
    LLVMValueRef d18 =
        LLVMBuildSub(bd, LLVMBuildZExt(bd, c18, c.i64, "c18z"), ss_c64(&c, '0'), "d18");
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntULT, d18, ss_c64(&c, 10), "more"), slow, fast);

    LLVMPositionBuilderAtEnd(bd, stop);
    LLVMBuildCondBr(bd, LLVMBuildICmp(bd, LLVMIntEQ, i, ss_c64(&c, 0), "none"), slow, fast);

    LLVMPositionBuilderAtEnd(bd, fast);
    LLVMValueRef fi = LLVMBuildPhi(bd, c.i64, "fi");
    LLVMValueRef facc = LLVMBuildPhi(bd, c.i64, "facc");
    LLVMAddIncoming(fi, (LLVMValueRef[]){i, i1}, (LLVMBasicBlockRef[]){stop, capped}, 2);
    LLVMAddIncoming(facc, (LLVMValueRef[]){acc, acc1}, (LLVMBasicBlockRef[]){stop, capped}, 2);
    LLVMBuildStore(bd, ss_gep(&c, p0, fi, "ep"), endp);
    LLVMBuildRet(bd, LLVMBuildSelect(bd, neg, LLVMBuildNeg(bd, facc, "nacc"), facc, "res"));

    LLVMPositionBuilderAtEnd(bd, slow);
    LLVMBuildRet(bd, zan_call2(bd, fnty, libc, (LLVMValueRef[]){s, endp, base}, 3, "libc"));
    if (saved) LLVMPositionBuilderAtEnd(bd, saved);
    return fn;
}
