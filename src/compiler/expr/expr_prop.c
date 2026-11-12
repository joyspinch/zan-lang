/* 自定义 getter 属性降解为方法调用，读取时发射其专用 getter 函数 */
zan_symbol_t *property_getter_sym(zan_irgen_t *g, zan_symbol_t *prop) {
    if (!prop || prop->kind != SYM_PROPERTY || !prop->decl) return NULL;
    if (!prop->decl->field_decl.getter_body) return NULL;
    zan_symbol_t *type_sym = prop->parent;
    if (!type_sym) return NULL;
    for (int i = 0; i < type_sym->member_count; i++) {
        zan_symbol_t *m = type_sym->members[i];
        if (!m || m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL) continue;
        zan_istr_t mn = m->name;
        if (mn.len == prop->name.len + 4 &&
            memcmp(mn.str, "get_", 4) == 0 &&
            memcmp(mn.str + 4, prop->name.str, prop->name.len) == 0)
            return m;
    }
    return NULL;
}

/* 在已求值的接收者上发射属性 getter 方法调用。
 * obj_ast describes recv for ownership cleanup, never the property result.
 * Implicit instance access supplies a borrowed AST_THIS_EXPR; static access
 * has no receiver and may supply NULL. */
LLVMValueRef emit_property_getter_call(zan_irgen_t *g,
                                              zan_symbol_t *getter,
                                              zan_type_t *recv_type,
                                              LLVMValueRef recv,
                                              zan_ast_node_t *obj_ast,
                                              local_scope_t *locals) {
    bool is_static = (getter->modifiers & MOD_STATIC) != 0;
    for (int fi = irgen_find_function(g, getter); fi >= 0; fi = -1) {
        if (g->functions[fi].sym == getter) {
            LLVMValueRef rval = recv;
            /* 无接收者访问实例属性时默认补齐 this 隐式指针 */
            if (!is_static && !rval) {
                zan_diag_emit_code(g->diag, DIAG_ERROR,
                              obj_ast ? obj_ast->loc : zan_loc(0, 0, 0, 0), 3000,
                              "property getter requires a receiver");
                return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
            }
            if (!is_static &&
                LLVMGetTypeKind(LLVMTypeOf(rval)) == LLVMStructTypeKind) {
                LLVMValueRef rslot = emit_entry_alloca(g, LLVMTypeOf(rval),
                                                       "pg.recv");
                LLVMBuildStore(g->builder, rval, rslot);
                rval = rslot;
            }
            LLVMTypeRef mft = g->functions[fi].fn_type;
            LLVMValueRef mfn = route_generic_method(g, recv_type, getter,
                g->functions[fi].fn, mft, &mft);
            LLVMValueRef args[1];
            unsigned argc = 0;
            if (!is_static) args[argc++] = rval;
            const char *cn = "pget";
            /* base.Prop 属性读取：绑定至基类属性 getter 实现 */
            LLVMValueRef result = emit_dispatch_call(g,
                (is_static || !recv_type ||
                 (obj_ast && obj_ast->kind == AST_BASE_EXPR))
                    ? NULL : recv_type->sym, getter,
                mfn, mft, args, (int)argc, cn);
            result = coerce_generic_result(g, result, getter, recv_type);
            /* 接收者强引用临时变量生命周期延长契约 */
            emit_release_owned_call_temp(g, obj_ast, recv, locals);
            return result;
        }
    }
    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
}

/* 获取属性 setter 方法符号 */
zan_symbol_t *property_setter_sym(zan_irgen_t *g, zan_symbol_t *prop) {
    if (!prop || prop->kind != SYM_PROPERTY || !prop->decl) return NULL;
    if (!prop->decl->field_decl.setter_body) return NULL;
    zan_symbol_t *type_sym = prop->parent;
    if (!type_sym) return NULL;
    for (int i = 0; i < type_sym->member_count; i++) {
        zan_symbol_t *m = type_sym->members[i];
        if (!m || m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL) continue;
        zan_istr_t mn = m->name;
        if (mn.len == prop->name.len + 4 &&
            memcmp(mn.str, "set_", 4) == 0 &&
            memcmp(mn.str + 4, prop->name.str, prop->name.len) == 0)
            return m;
    }
    return NULL;
}

/* 在接收者上发射 set_Prop(value) 调用，传入右值作为实参 */
void emit_property_setter_call(zan_irgen_t *g, zan_symbol_t *setter,
                                      zan_type_t *recv_type, LLVMValueRef recv,
                                      LLVMValueRef value,
                                      zan_ast_node_t *obj_ast,
                                      zan_ast_node_t *rhs_ast,
                                      local_scope_t *locals) {
    bool is_static = (setter->modifiers & MOD_STATIC) != 0;
    for (int fi = irgen_find_function(g, setter); fi >= 0; fi = -1) {
        if (g->functions[fi].sym == setter) {
            LLVMValueRef rval = recv;
            /* 实例 setter 空接收者防护：自动使用当前 this 指针 */
            if (!is_static && !rval) {
                zan_diag_emit_code(g->diag, DIAG_ERROR,
                              obj_ast ? obj_ast->loc : zan_loc(0, 0, 0, 0), 3001,
                              "property setter requires a receiver");
                return;
            }
            if (!is_static &&
                LLVMGetTypeKind(LLVMTypeOf(rval)) == LLVMStructTypeKind) {
                LLVMValueRef rslot = emit_entry_alloca(g, LLVMTypeOf(rval),
                                                       "ps.recv");
                LLVMBuildStore(g->builder, rval, rslot);
                rval = rslot;
            }
            LLVMTypeRef mft = g->functions[fi].fn_type;
            LLVMValueRef mfn = route_generic_method(g, recv_type, setter,
                g->functions[fi].fn, mft, &mft);
            /* 属性 setter 赋值实参位置绑定 */
            zan_type_t *p0 = method_param_type_at(g, setter, 0, NULL,
                                                  obj_ast, locals);
            if (p0) p0 = subst_type_param_deep(g, p0, recv_type);
            LLVMValueRef v = value;
            if (p0) {
                LLVMTypeRef p0t = map_type(g, p0);
                if (LLVMTypeOf(v) != p0t)
                    v = coerce_int_to(g, v, p0t);
            }
            LLVMValueRef args[2];
            unsigned argc = 0;
            if (!is_static) args[argc++] = rval;
            args[argc++] = v;
            /* 裸属性名赋值 Prop = v 降解处理 */
            emit_dispatch_call(g, (is_static || !recv_type ||
                (obj_ast && obj_ast->kind == AST_BASE_EXPR))
                    ? NULL : recv_type->sym, setter, mfn, mft, args, (int)argc, "");
            emit_release_owned_call_temp(g, obj_ast, recv, locals);
            emit_release_owned_call_temp(g, rhs_ast, value, locals);
            return;
        }
    }
}

