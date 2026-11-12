int zan_irgen_shard_run(zan_irgen_t *g, const zan_cg_manifest_t *m,
                        const char *obj_base, char ***out_objs) {
    *out_objs = NULL;
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

    sh_map_t mf2mem; sh_map_init(&mf2mem);
    for (int k = 0; k < nm; k++) sh_map_put(&mf2mem, mem[k].fn, k);

    /* 脏不动点迭代：剔除无法通过外部链接解析的跨分片成员 */
    bool changed = true;
    int iter_guard = 0;
    while (changed && iter_guard++ < 64) {
        changed = false;
        for (int k = 0; k < nm; k++) {
            sh_member_t *M = &mem[k];
            if (M->comp < 0) continue;
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

    /* 装箱启发式分片：防止调用图合并为超大连通块导致分片倾斜 */
    for (int k = 0; k < nm; k++) {
        if (mem[k].comp < 0) continue;
        mem[k].comp = k;
    }

    /* ---- components ---- */
    sh_map_t root2comp; sh_map_init(&root2comp);
    sh_comp_t *comps = (sh_comp_t *)calloc((size_t)nm, sizeof(*comps));
    int ncomp = 0;
    bool comp_ok = comps != NULL;
    for (int k = 0; k < nm && comp_ok; k++) {
        if (mem[k].comp < 0) continue;
        int root = mem[k].comp;
        int ci = sh_map_get(&root2comp, mem[root].fn);
        if (ci < 0) {
            ci = ncomp++;
            if (!sh_map_put(&root2comp, mem[root].fn, ci)) {
                comp_ok = false; break;
            }
            comps[ci].minname = LLVMGetValueName(mem[k].fn);
        }
        /* 单节点连通块容量优化 */
        if (comps[ci].n == comps[ci].cap) {
            if (comps[ci].cap > INT_MAX / 2) { comp_ok = false; break; }
            int cap = comps[ci].cap ? comps[ci].cap * 2 : 1;
            int *idx = (int *)realloc(comps[ci].idx, (size_t)cap * sizeof(*idx));
            if (!idx) { comp_ok = false; break; }
            comps[ci].idx = idx;
            comps[ci].cap = cap;
        }
        comps[ci].idx[comps[ci].n++] = k;
        comps[ci].insns += m->fns[mem[k].mf_i].insns;
        const char *nm2 = LLVMGetValueName(mem[k].fn);
        if (strcmp(nm2, comps[ci].minname) < 0) comps[ci].minname = nm2;
    }
    if (!comp_ok) {
        for (int c = 0; c < ncomp; c++) free(comps[c].idx);
        free(comps); free(uf); free(mem);
        sh_map_free(&root2comp); sh_map_free(&mf2mem);
        sh_map_free(&st.fn_v); sh_map_free(&st.glob_v);
        sh_map_free(&st.members_all); sh_map_free(&st.needs_decl);
        sh_map_free(&st.ext_globs); sh_map_free(&st.ext_fns);
        free(st.link_recs);
        fprintf(stderr, "shard: out of memory planning components — single module\n");
        return 0;
    }
    qsort(comps, (size_t)ncomp, sizeof(*comps), sh_cmp_comp);

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
        for (int c = 0; c < ncomp; c++) free(comps[c].idx);
        free(comps); free(shard_of_comp); free(mem);
        sh_map_free(&mf2mem);
        sh_map_free(&st.fn_v); sh_map_free(&st.glob_v);
        sh_map_free(&st.members_all);
        sh_map_free(&st.needs_decl);
        return 0;
    }

    for (int c = 0; c < ncomp; c++)
        for (int j = 0; j < comps[c].n; j++)
            mem[comps[c].idx[j]].comp = shard_of_comp[c];

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
                    continue;
                unsigned nop = LLVMGetNumOperands(in);
                for (unsigned k = 0; k < nop; k++)
                    sh_planner_mark(&st, LLVMGetOperand(in, (int)k), 0);
            }
        }
    }


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

    /* 第 1 遍：分片引用遍历并填充 needs_decl 需求表 */
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
        /* 回滚第 1 遍已外化修改的全局变量属性 */
        for (int i = 0; i < st.link_n; i++)
            LLVMSetLinkage(st.link_recs[i].glob ? st.link_recs[i].glob
                                               : st.link_recs[i].fn,
                           st.link_recs[i].old_linkage);
        fprintf(stderr, "shard: %s — falling back to single module\n",
                st.reason);
        goto cleanup_planning;
    }

    /* 调整外部引用函数的符号链接为 external */
    for (int k = 0; k < nm; k++) {
        if (mem[k].comp < 0) continue;
        if (sh_map_get(&st.needs_decl, mem[k].fn) >= 0) {
            sh_link_rec(&st, mem[k].fn, false);
            LLVMSetLinkage(mem[k].fn, LLVMExternalLinkage);
        }
    }
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

    char tmp_ll[1200];
    tmp_ll[0] = '\0';
    FILE *body_file = NULL;
    sh_body_index_t body_idx = {0};
    if (!st.failed) {
        if (!obj_base || !*obj_base) {
            sh_fail(&st, "missing shard output path");
        } else if (snprintf(tmp_ll, sizeof(tmp_ll), "%s.shard.tmp.ll", obj_base) >=
                   (int)sizeof(tmp_ll)) {
            tmp_ll[0] = '\0';
            sh_fail(&st, "shard module text path too long");
        } else {
            char *err_msg = NULL;
            if (LLVMPrintModuleToFile(g->mod, tmp_ll, &err_msg)) {
                sh_fail(&st, "cannot write shard module text: %.160s",
                        err_msg ? err_msg : "?");
            } else {
                body_file = fopen(tmp_ll, "rb");
                if (!body_file) {
                    sh_fail(&st, "cannot open shard module text (errno=%d)", errno);
                } else if (!sh_build_body_index(&body_idx, body_file)) {
                    sh_fail(&st, "cannot build complete shard body index");
                }
            }
            if (err_msg) LLVMDisposeMessage(err_msg);
        }
    }

    for (int s = 0; s < nshard && !st.failed; s++) {
        SH_TRACE("shard %d: assembling\n", s);
        sh_sbuf_t bodies = {0}, gdecls = {0}, types = {0};
        sh_map_t decl_fns, decl_globs, travel;
        sh_map_init(&decl_fns); sh_map_init(&decl_globs); sh_map_init(&travel);
        sh_map_init(&st.type_done);

        bool ok = true;
        sh_map_t local; sh_map_init(&local);
        sh_map_t closure_memo; sh_map_init(&closure_memo);
        for (int c = 0; c < ncomp && ok; c++) {
            if (shard_of_comp[c] != s) continue;
            for (int j = 0; j < comps[c].n; j++)
                if (!sh_map_put(&local, mem[comps[c].idx[j]].fn, 1)) ok = false;
        }

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
                const char *fn_name = LLVMGetValueName(fn);
                const sh_body_slice_t *sl = sh_find_body_slice(&body_idx, fn_name);
                if (!sl) {
                    sh_fail(&st, "body '%s' missing from shard module text", fn_name);
                    ok = false;
                    break;
                }
                size_t body_start = bodies.n;
                if (!sh_append_body_slice(body_file, sl, &bodies)) {
                    sh_fail(&st, "cannot read body '%s' from shard module text", fn_name);
                    ok = false;
                    break;
                }
                /* 检查完整 define 头，无需克隆函数体 */
                char *header = bodies.p + body_start;
                char *hnl = (char *)memchr(header, '\n', sl->len);
                if (hnl) *hnl = '\0';
                bool has_comdat = strstr(header, " comdat") != NULL;
                if (hnl) *hnl = '\n';
                if (has_comdat) {
                    sh_fail(&st, "body '%s' carries comdat", fn_name);
                    ok = false;
                    break;
                }
                sh_sb_puts(&bodies, "\n");
            }
            free(mbrs);
        }

        SH_TRACE("shard %d: bodies done (%d bytes)\n", s, (int)bodies.n);
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
        if (ok) {
            /* 合并跨分片引用的外部声明标记集合 */
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
        if (ok) {
            sh_harvest_types(&st, &types, bodies.p ? bodies.p : "");
            sh_harvest_types(&st, &types, gdecls.p ? gdecls.p : "");
        }

        /* 直接流式输出三段内容以降低内存驻留 */
        if (bodies.oom || gdecls.oom || types.oom) {
            sh_fail(&st, "out of memory assembling fragment");
            ok = false;
        }
        if (st.failed) ok = false;
        if (ok) {
            char frag_path[1200];
            snprintf(frag_path, sizeof(frag_path), "%s.shard%d.frag.ll", obj_base, s);
            FILE *ff = fopen(frag_path, "wb");
            if (ff) {
                bool wrote = (!types.n || fwrite(types.p, 1, types.n, ff) == types.n) &&
                             (!gdecls.n || fwrite(gdecls.p, 1, gdecls.n, ff) == gdecls.n) &&
                             (!bodies.n || fwrite(bodies.p, 1, bodies.n, ff) == bodies.n);
                if (fclose(ff)) wrote = false;
                if (!wrote) {
                    sh_fail(&st, "cannot write complete shard fragment '%s'", frag_path);
                    ok = false;
                }
            } else {
                sh_fail(&st, "cannot open shard fragment file '%s' for write (errno=%d)", frag_path, errno);
                ok = false;
            }
        } else if (!st.failed) {
            sh_fail(&st, "shard %d: assembling failed before write", s);
        }

        sh_map_free(&local);
        sh_map_free(&closure_memo);
        sh_map_free(&decl_fns); sh_map_free(&decl_globs); sh_map_free(&travel);
        sh_map_free(&st.type_done);
        st.type_done.keys = NULL; st.type_done.vals = NULL;
        free(bodies.p); free(gdecls.p); free(types.p);
    }

    sh_free_body_index(&body_idx);
    if (body_file) fclose(body_file);
    if (tmp_ll[0]) {
        remove(tmp_ll);
        tmp_ll[0] = '\0';
    }

    /* 第 2b 遍：从片段文件流式发射分片，限制峰值内存占用 */
    for (int s = 0; s < nshard && !st.failed; s++) {
        char frag_path[1200];
        snprintf(frag_path, sizeof(frag_path), "%s.shard%d.frag.ll", obj_base, s);
        char path[1200];
        snprintf(path, sizeof(path), "%s.shard%d.o", obj_base, s);
        char errbuf[256];
        char ptag[64];
        snprintf(ptag, sizeof(ptag), "shard %d: before emit", s);
        sh_probe_mem(ptag);
        if (sh_emit_one_file(&st, s, frag_path, tm, path, errbuf, sizeof(errbuf))) {
            objs[s] = (char *)malloc(strlen(path) + 1);
            if (objs[s]) strcpy(objs[s], path);
            else sh_fail(&st, "out of memory");
        } else {
            sh_fail(&st, "shard %d: %s", s, errbuf);
            remove(path);
        }
        remove(frag_path);
        snprintf(ptag, sizeof(ptag), "shard %d: after emit", s);
        sh_probe_mem(ptag);
    }

    if (st.failed) {
        for (int s = 0; s < nshard; s++) {
            char frag_path[1200];
            snprintf(frag_path, sizeof(frag_path), "%s.shard%d.frag.ll", obj_base, s);
            remove(frag_path);
        }
        for (int i = 0; i < st.link_n; i++)
            LLVMSetLinkage(st.link_recs[i].glob ? st.link_recs[i].glob
                                               : st.link_recs[i].fn,
                           st.link_recs[i].old_linkage);
        if (objs)
            for (int s = 0; s < nshard; s++) {
                if (objs[s]) { remove(objs[s]); free(objs[s]); }
            }
        free(objs);
        if (st.optimizer_failed) {
            fprintf(stderr, "error: shard: %s\n", st.reason);
            rc = -1;
        } else {
            fprintf(stderr, "shard: %s — falling back to single module\n",
                    st.reason);
        }
        goto cleanup_planning;
    }

    /* 符号迁移：重命名、外部声明并执行 RAUW 批量替换 */
    {
        bool moved_ok = true;
        int rename_counter = 0;
        sh_move_rec_t *moves = (sh_move_rec_t *)calloc((size_t)nm,
                                                        sizeof(*moves));
        int nmoves = 0;
        for (int k = 0; k < nm && moved_ok; k++) {
            if (mem[k].comp < 0) continue;
            if (sh_map_get(&st.needs_decl, mem[k].fn) < 0) continue;
            LLVMValueRef orig = mem[k].fn;
            /* 复制 LLVMGetValueName 内部字符串以防重命名后内存失效 */
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
            /* 回滚：恢复函数使用点、原始符号名称与链接属性并删除声明 */
            for (int i = 0; i < nmoves; i++) {
                LLVMReplaceAllUsesWith(moves[i].decl, moves[i].orig);
                LLVMDeleteFunction(moves[i].decl);
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
        for (int k = 0; k < nm; k++) {
            if (mem[k].comp < 0) continue;
            LLVMValueRef orig = mem[k].fn;
            if (LLVMGetFirstUse(orig)) {
                if (sh_map_get(&st.needs_decl, orig) >= 0) {
                    /* 阶段 A 生成干净名称的声明供分片导出 */
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
                fprintf(stderr,
                        "error: shard: coordinator verify failed after delete: "
                        "%.160s\n", vmsg ? vmsg : "?");
                if (vmsg) LLVMDisposeMessage(vmsg);
                rc = -1;
            } else {
                if (sh_trace_on()) {
                    /* 校验所有迁移函数的引用点，防止链接期未定义符号 */
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
