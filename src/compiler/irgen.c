/* irgen.c -- LLVM IR generation for the Zan language.
 *
 * For M1 this generates code for:
 *   - Static Main() method as program entry point
 *   - Console.WriteLine() calls → zan_rt_println / printf
 *   - Integer and floating-point arithmetic
 *   - Local variable declarations and assignments
 *   - Control flow (if, while, for)
 *   - String literals
 */

#include "irgen.h"
#include "arena.h"
#include "diag.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- initialization ---- */

zan_status_t zan_irgen_init(zan_irgen_t *g, zan_arena_t *arena,
                            zan_diag_t *diag, zan_binder_t *binder,
                            const char *module_name) {
    memset(g, 0, sizeof(*g));
    g->arena = arena;
    g->diag = diag;
    g->binder = binder;

    g->ctx = LLVMContextCreate();
    g->mod = LLVMModuleCreateWithNameInContext(module_name, g->ctx);
    g->builder = LLVMCreateBuilderInContext(g->ctx);

    /* declare printf */
    LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) };
    LLVMTypeRef printf_type = LLVMFunctionType(
        LLVMInt32TypeInContext(g->ctx), printf_args, 1, 1 /* varargs */);
    LLVMValueRef printf_fn = LLVMAddFunction(g->mod, "printf", printf_type);

    /* declare zan_rt_println(const char*) → calls printf("%s\n", str) */
    LLVMTypeRef println_args[] = { LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) };
    LLVMTypeRef println_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), println_args, 1, 0);
    g->rt_println = LLVMAddFunction(g->mod, "zan_rt_println", println_type);

    /* implement zan_rt_println */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_println, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    LLVMValueRef fmt_str = LLVMBuildGlobalStringPtr(g->builder, "%s\n", "println_fmt");
    LLVMValueRef args[] = { fmt_str, LLVMGetParam(g->rt_println, 0) };
    LLVMBuildCall2(g->builder, printf_type, printf_fn, args, 2, "");
    LLVMBuildRetVoid(g->builder);

    /* declare zan_rt_print_int(int64) */
    LLVMTypeRef pint_args[] = { LLVMInt64TypeInContext(g->ctx) };
    LLVMTypeRef pint_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), pint_args, 1, 0);
    g->rt_print_int = LLVMAddFunction(g->mod, "zan_rt_print_int", pint_type);

    LLVMBasicBlockRef pint_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_int, "entry");
    LLVMPositionBuilderAtEnd(g->builder, pint_entry);
    LLVMValueRef int_fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld\n", "int_fmt");
    LLVMValueRef iargs[] = { int_fmt, LLVMGetParam(g->rt_print_int, 0) };
    LLVMBuildCall2(g->builder, printf_type, printf_fn, iargs, 2, "");
    LLVMBuildRetVoid(g->builder);

    /* declare zan_rt_print_double(double) */
    LLVMTypeRef pdbl_args[] = { LLVMDoubleTypeInContext(g->ctx) };
    LLVMTypeRef pdbl_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), pdbl_args, 1, 0);
    g->rt_print_double = LLVMAddFunction(g->mod, "zan_rt_print_double", pdbl_type);

    LLVMBasicBlockRef pdbl_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_double, "entry");
    LLVMPositionBuilderAtEnd(g->builder, pdbl_entry);
    LLVMValueRef dbl_fmt = LLVMBuildGlobalStringPtr(g->builder, "%g\n", "dbl_fmt");
    LLVMValueRef dargs[] = { dbl_fmt, LLVMGetParam(g->rt_print_double, 0) };
    LLVMBuildCall2(g->builder, printf_type, printf_fn, dargs, 2, "");
    LLVMBuildRetVoid(g->builder);

    /* declare C library functions for string interpolation */
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);

    /* int snprintf(char*, size_t, const char*, ...) */
    LLVMTypeRef snprintf_args[] = { i8ptr, i64, i8ptr };
    LLVMTypeRef snprintf_type = LLVMFunctionType(i32, snprintf_args, 3, 1);
    g->fn_snprintf = LLVMAddFunction(g->mod, "snprintf", snprintf_type);

    /* void *malloc(size_t) */
    LLVMTypeRef malloc_args[] = { i64 };
    LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, malloc_args, 1, 0);
    g->fn_malloc = LLVMAddFunction(g->mod, "malloc", malloc_type);

    /* void free(void*) */
    LLVMTypeRef free_args[] = { i8ptr };
    LLVMTypeRef free_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), free_args, 1, 0);
    g->fn_free = LLVMAddFunction(g->mod, "free", free_type);

    /* size_t strlen(const char*) */
    LLVMTypeRef strlen_args[] = { i8ptr };
    LLVMTypeRef strlen_type = LLVMFunctionType(i64, strlen_args, 1, 0);
    g->fn_strlen = LLVMAddFunction(g->mod, "strlen", strlen_type);

    /* char *strcpy(char*, const char*) */
    LLVMTypeRef strcpy_args[] = { i8ptr, i8ptr };
    LLVMTypeRef strcpy_type = LLVMFunctionType(i8ptr, strcpy_args, 2, 0);
    g->fn_strcpy = LLVMAddFunction(g->mod, "strcpy", strcpy_type);

    /* char *strcat(char*, const char*) */
    LLVMTypeRef strcat_args[] = { i8ptr, i8ptr };
    LLVMTypeRef strcat_type = LLVMFunctionType(i8ptr, strcat_args, 2, 0);
    g->fn_strcat = LLVMAddFunction(g->mod, "strcat", strcat_type);

    return ZAN_OK;
}

void zan_irgen_destroy(zan_irgen_t *g) {
    if (g->builder) LLVMDisposeBuilder(g->builder);
    if (g->mod) LLVMDisposeModule(g->mod);
    if (g->ctx) LLVMContextDispose(g->ctx);
}

/* ---- type mapping ---- */

static LLVMTypeRef map_type(zan_irgen_t *g, zan_type_t *type) {
    if (!type) return LLVMVoidTypeInContext(g->ctx);
    switch (type->kind) {
    case TYPE_VOID:   return LLVMVoidTypeInContext(g->ctx);
    case TYPE_BOOL:   return LLVMInt1TypeInContext(g->ctx);
    case TYPE_BYTE:   return LLVMInt8TypeInContext(g->ctx);
    case TYPE_SHORT:  return LLVMInt16TypeInContext(g->ctx);
    case TYPE_INT:    return LLVMInt32TypeInContext(g->ctx);
    case TYPE_LONG:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_NINT:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_FLOAT:  return LLVMFloatTypeInContext(g->ctx);
    case TYPE_DOUBLE: return LLVMDoubleTypeInContext(g->ctx);
    case TYPE_CHAR:   return LLVMInt32TypeInContext(g->ctx);
    case TYPE_STRING: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_OBJECT: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_STRUCT:
    case TYPE_CLASS: {
        /* look up registered struct type */
        for (int i = 0; i < g->struct_type_count; i++) {
            if (g->struct_types[i].sym == type->sym) {
                if (type->kind == TYPE_CLASS) {
                    return LLVMPointerType(g->struct_types[i].llvm_type, 0);
                }
                return g->struct_types[i].llvm_type;
            }
        }
        return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
    default:          return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
}

/* ---- struct type registry helpers ---- */

static LLVMTypeRef get_struct_llvm_type(zan_irgen_t *g, zan_symbol_t *sym) {
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == sym) return g->struct_types[i].llvm_type;
    }
    return NULL;
}

static int get_field_index(zan_symbol_t *type_sym, zan_istr_t field_name) {
    int idx = 0;
    for (int i = 0; i < type_sym->member_count; i++) {
        if (type_sym->members[i]->kind == SYM_FIELD) {
            if (type_sym->members[i]->name.len == field_name.len &&
                memcmp(type_sym->members[i]->name.str, field_name.str, field_name.len) == 0) {
                return idx;
            }
            idx++;
        }
    }
    return -1;
}

static zan_symbol_t *get_field_sym(zan_symbol_t *type_sym, zan_istr_t field_name) {
    for (int i = 0; i < type_sym->member_count; i++) {
        if (type_sym->members[i]->kind == SYM_FIELD &&
            type_sym->members[i]->name.len == field_name.len &&
            memcmp(type_sym->members[i]->name.str, field_name.str, field_name.len) == 0) {
            return type_sym->members[i];
        }
    }
    return NULL;
}

static zan_symbol_t *get_method_sym(zan_symbol_t *type_sym, zan_istr_t method_name) {
    for (int i = 0; i < type_sym->member_count; i++) {
        if (type_sym->members[i]->kind == SYM_METHOD &&
            type_sym->members[i]->name.len == method_name.len &&
            memcmp(type_sym->members[i]->name.str, method_name.str, method_name.len) == 0) {
            return type_sym->members[i];
        }
    }
    return NULL;
}

static void register_struct_type(zan_irgen_t *g, zan_symbol_t *sym) {
    if (g->struct_type_count >= 256) return;
    if (get_struct_llvm_type(g, sym)) return;

    /* count fields and build LLVM field types */
    int field_count = 0;
    for (int i = 0; i < sym->member_count; i++) {
        if (sym->members[i]->kind == SYM_FIELD) field_count++;
    }

    LLVMTypeRef *field_types = (LLVMTypeRef *)calloc((size_t)field_count, sizeof(LLVMTypeRef));
    int fi = 0;
    for (int i = 0; i < sym->member_count; i++) {
        if (sym->members[i]->kind == SYM_FIELD) {
            field_types[fi++] = map_type(g, sym->members[i]->type);
        }
    }

    /* create named struct */
    char name_buf[256];
    snprintf(name_buf, sizeof(name_buf), "struct.%.*s", (int)sym->name.len, sym->name.str);
    LLVMTypeRef st = LLVMStructCreateNamed(g->ctx, name_buf);
    LLVMStructSetBody(st, field_types, (unsigned)field_count, 0);

    g->struct_types[g->struct_type_count].sym = sym;
    g->struct_types[g->struct_type_count].llvm_type = st;
    g->struct_type_count++;

    free(field_types);
}

/* ---- local variables (simple stack-based storage) ---- */

#define MAX_LOCALS 256

typedef struct {
    zan_istr_t name;
    LLVMValueRef alloca;
    zan_type_t *type;
} local_var_t;

typedef struct {
    local_var_t vars[MAX_LOCALS];
    int count;
} local_scope_t;

static local_scope_t *local_scope_new(zan_arena_t *arena) {
    local_scope_t *s = (local_scope_t *)zan_arena_alloc(arena, sizeof(local_scope_t));
    s->count = 0;
    return s;
}

static void local_add(local_scope_t *scope, zan_istr_t name, LLVMValueRef alloca, zan_type_t *type) {
    if (scope->count < MAX_LOCALS) {
        scope->vars[scope->count].name = name;
        scope->vars[scope->count].alloca = alloca;
        scope->vars[scope->count].type = type;
        scope->count++;
    }
}

static local_var_t *local_find(local_scope_t *scope, zan_istr_t name) {
    for (int i = scope->count - 1; i >= 0; i--) {
        if (scope->vars[i].name.len == name.len &&
            memcmp(scope->vars[i].name.str, name.str, (size_t)name.len) == 0) {
            return &scope->vars[i];
        }
    }
    return NULL;
}

/* ---- expression codegen ---- */

static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);

static bool is_call_to(zan_ast_node_t *expr, const char *obj, const char *method) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;
    if (callee->member.object->kind != AST_IDENTIFIER) return false;

    zan_istr_t obj_name = callee->member.object->ident.name;
    zan_istr_t method_name = callee->member.name;

    return ((int)obj_name.len == (int)strlen(obj) &&
            memcmp(obj_name.str, obj, (size_t)obj_name.len) == 0 &&
            (int)method_name.len == (int)strlen(method) &&
            memcmp(method_name.str, method, (size_t)method_name.len) == 0);
}

static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals) {
    if (!expr) return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);

    switch (expr->kind) {
    case AST_INT_LITERAL:
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), (uint64_t)expr->int_val, 1);

    case AST_FLOAT_LITERAL:
        return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), expr->float_val);

    case AST_STRING_LITERAL:
        return LLVMBuildGlobalStringPtr(g->builder, expr->str_val.str, "str");

    case AST_BOOL_LITERAL:
        return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), expr->bool_val ? 1 : 0, 0);

    case AST_CHAR_LITERAL:
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), (uint64_t)expr->int_val, 0);

    case AST_NULL_LITERAL:
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));

    case AST_IDENTIFIER: {
        local_var_t *local = local_find(locals, expr->ident.name);
        if (local) {
            return LLVMBuildLoad2(g->builder, map_type(g, local->type),
                                 local->alloca, "load");
        }
        /* implicit this.Field access in method bodies */
        if (g->current_this && g->current_type_sym) {
            int fi = get_field_index(g->current_type_sym, expr->ident.name);
            if (fi >= 0) {
                LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                if (st) {
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, this_ptr, (unsigned)fi, "fld");
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->ident.name);
                    LLVMTypeRef ft = fsym ? map_type(g, fsym->type) : LLVMInt64TypeInContext(g->ctx);
                    return LLVMBuildLoad2(g->builder, ft, fptr, "fval");
                }
            }
        }
        /* return 0 for unresolved — error was reported in checker */
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    }

    case AST_BINARY: {
        LLVMValueRef left = emit_expr(g, expr->binary.left, locals);
        LLVMValueRef right = emit_expr(g, expr->binary.right, locals);

        LLVMTypeRef left_type = LLVMTypeOf(left);
        bool is_float = (LLVMGetTypeKind(left_type) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(left_type) == LLVMFloatTypeKind);

        switch (expr->binary.op) {
        case TK_PLUS:
            return is_float ? LLVMBuildFAdd(g->builder, left, right, "add")
                            : LLVMBuildAdd(g->builder, left, right, "add");
        case TK_MINUS:
            return is_float ? LLVMBuildFSub(g->builder, left, right, "sub")
                            : LLVMBuildSub(g->builder, left, right, "sub");
        case TK_STAR:
            return is_float ? LLVMBuildFMul(g->builder, left, right, "mul")
                            : LLVMBuildMul(g->builder, left, right, "mul");
        case TK_SLASH:
            return is_float ? LLVMBuildFDiv(g->builder, left, right, "div")
                            : LLVMBuildSDiv(g->builder, left, right, "div");
        case TK_PERCENT:
            return is_float ? LLVMBuildFRem(g->builder, left, right, "rem")
                            : LLVMBuildSRem(g->builder, left, right, "rem");
        case TK_AMP:
            return LLVMBuildAnd(g->builder, left, right, "and");
        case TK_PIPE:
            return LLVMBuildOr(g->builder, left, right, "or");
        case TK_CARET:
            return LLVMBuildXor(g->builder, left, right, "xor");
        case TK_LESS_LESS:
            return LLVMBuildShl(g->builder, left, right, "shl");
        case TK_GREATER_GREATER:
            return LLVMBuildAShr(g->builder, left, right, "shr");
        case TK_EQ_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOEQ, left, right, "eq")
                            : LLVMBuildICmp(g->builder, LLVMIntEQ, left, right, "eq");
        case TK_BANG_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealONE, left, right, "ne")
                            : LLVMBuildICmp(g->builder, LLVMIntNE, left, right, "ne");
        case TK_LESS:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLT, left, right, "lt")
                            : LLVMBuildICmp(g->builder, LLVMIntSLT, left, right, "lt");
        case TK_GREATER:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGT, left, right, "gt")
                            : LLVMBuildICmp(g->builder, LLVMIntSGT, left, right, "gt");
        case TK_LESS_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLE, left, right, "le")
                            : LLVMBuildICmp(g->builder, LLVMIntSLE, left, right, "le");
        case TK_GREATER_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGE, left, right, "ge")
                            : LLVMBuildICmp(g->builder, LLVMIntSGE, left, right, "ge");
        default:
            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
        }
    }

    case AST_UNARY: {
        LLVMValueRef operand = emit_expr(g, expr->unary.operand, locals);
        switch (expr->unary.op) {
        case TK_MINUS: {
            LLVMTypeRef t = LLVMTypeOf(operand);
            if (LLVMGetTypeKind(t) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(t) == LLVMFloatTypeKind) {
                return LLVMBuildFNeg(g->builder, operand, "neg");
            }
            return LLVMBuildNeg(g->builder, operand, "neg");
        }
        case TK_BANG:
            return LLVMBuildNot(g->builder, operand, "not");
        case TK_TILDE:
            return LLVMBuildNot(g->builder, operand, "bnot");
        default:
            return operand;
        }
    }

    case AST_ASSIGNMENT: {
        LLVMValueRef right = emit_expr(g, expr->binary.right, locals);
        if (expr->binary.left->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->binary.left->ident.name);
            if (local) {
                LLVMBuildStore(g->builder, right, local->alloca);
            } else if (g->current_this && g->current_type_sym) {
                /* implicit this.Field assignment */
                int fi = get_field_index(g->current_type_sym, expr->binary.left->ident.name);
                if (fi >= 0) {
                    LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                    if (st) {
                        LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                            LLVMPointerType(st, 0), g->current_this, "this");
                        LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, this_ptr, (unsigned)fi, "fld");
                        /* type conversion if needed */
                        zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->binary.left->ident.name);
                        if (fsym && fsym->type) {
                            LLVMTypeRef target_t = map_type(g, fsym->type);
                            LLVMTypeRef val_t = LLVMTypeOf(right);
                            if (target_t != val_t) {
                                if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                    right = LLVMBuildFPTrunc(g->builder, right, target_t, "trunc");
                                } else if (LLVMGetTypeKind(target_t) == LLVMDoubleTypeKind &&
                                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                                    right = LLVMBuildFPExt(g->builder, right, target_t, "ext");
                                } else if (LLVMGetTypeKind(target_t) == LLVMIntegerTypeKind &&
                                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind) {
                                    unsigned tw = LLVMGetIntTypeWidth(target_t);
                                    unsigned vw = LLVMGetIntTypeWidth(val_t);
                                    if (tw > vw) right = LLVMBuildSExt(g->builder, right, target_t, "ext");
                                    else if (tw < vw) right = LLVMBuildTrunc(g->builder, right, target_t, "trunc");
                                }
                            }
                        }
                        LLVMBuildStore(g->builder, right, fptr);
                    }
                }
            }
        } else if (expr->binary.left->kind == AST_INDEX) {
            /* arr[i] = value */
            zan_ast_node_t *arr_expr = expr->binary.left->index.object;
            if (arr_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, arr_expr->ident.name);
                if (local) {
                    LLVMValueRef arr_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                        local->alloca, "arrload");
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                    if (local->type && local->type->element_type) {
                        elem_llvm = map_type(g, local->type->element_type);
                    }
                    LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, arr_ptr, &idx, 1, "eidx");
                    LLVMBuildStore(g->builder, right, elem_ptr);
                }
            }
        } else if (expr->binary.left->kind == AST_MEMBER_ACCESS) {
            /* obj.Field = value */
            zan_ast_node_t *obj_expr = expr->binary.left->member.object;
            if (obj_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, obj_expr->ident.name);
                if (local && local->type && local->type->sym) {
                    int fi = get_field_index(local->type->sym, expr->binary.left->member.name);
                    if (fi >= 0) {
                        LLVMTypeRef st = get_struct_llvm_type(g, local->type->sym);
                        if (st) {
                            LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, local->alloca, (unsigned)fi, "fld");
                            LLVMBuildStore(g->builder, right, fptr);
                        }
                    }
                }
            }
        }
        return right;
    }

    case AST_CALL: {
        /* special-case Console.WriteLine */
        if (is_call_to(expr, "Console", "WriteLine") ||
            is_call_to(expr, "Console", "PrintLine")) {
            if (expr->call.args.count > 0) {
                LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                LLVMTypeRef arg_type = LLVMTypeOf(arg);

                if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) },
                        1, 0);
                    LLVMBuildCall2(g->builder, fn_type, g->rt_println, &arg, 1, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                           LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMDoubleTypeInContext(g->ctx) },
                        1, 0);
                    LLVMBuildCall2(g->builder, fn_type, g->rt_print_double, &arg, 1, "");
                } else {
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMInt64TypeInContext(g->ctx) },
                        1, 0);
                    LLVMBuildCall2(g->builder, fn_type, g->rt_print_int, &arg, 1, "");
                }
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Math.Sqrt(expr) → llvm.sqrt */
        if (is_call_to(expr, "Math", "Sqrt") && expr->call.args.count == 1) {
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            /* ensure arg is double */
            if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMFloatTypeKind) {
                arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
            } else if (LLVMGetTypeKind(LLVMTypeOf(arg)) != LLVMDoubleTypeKind) {
                arg = LLVMBuildSIToFP(g->builder, arg, dbl, "tofp");
            }
            /* declare sqrt if needed */
            LLVMValueRef sqrt_fn = LLVMGetNamedFunction(g->mod, "sqrt");
            if (!sqrt_fn) {
                LLVMTypeRef sqrt_args[] = { dbl };
                LLVMTypeRef sqrt_type = LLVMFunctionType(dbl, sqrt_args, 1, 0);
                sqrt_fn = LLVMAddFunction(g->mod, "sqrt", sqrt_type);
            }
            LLVMTypeRef sqrt_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
            return LLVMBuildCall2(g->builder, sqrt_type, sqrt_fn, &arg, 1, "sqrt");
        }

        /* user-defined method call: obj.Method(args) or Type.StaticMethod(args) */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            if (callee->member.object->kind == AST_IDENTIFIER) {
                /* first try as instance method: local_var.Method(args) */
                local_var_t *local = local_find(locals, callee->member.object->ident.name);
                if (local && local->type && local->type->sym) {
                    zan_symbol_t *type_sym = local->type->sym;
                    zan_symbol_t *method_sym = get_method_sym(type_sym, callee->member.name);
                    if (method_sym) {
                        for (int fi = 0; fi < g->function_count; fi++) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count + 1;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                                call_args[0] = local->alloca;
                                for (int k = 0; k < expr->call.args.count; k++) {
                                    call_args[k + 1] = emit_expr(g, expr->call.args.items[k], locals);
                                }
                                LLVMValueRef result = LLVMBuildCall2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, call_args, (unsigned)argc, "mcall");
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }

                /* try as static method: ClassName.Method(args) */
                zan_symbol_t *type_sym = zan_binder_lookup(g->binder, callee->member.object->ident.name);
                if (type_sym && (type_sym->kind == SYM_CLASS || type_sym->kind == SYM_STRUCT)) {
                    zan_symbol_t *method_sym = get_method_sym(type_sym, callee->member.name);
                    if (method_sym) {
                        for (int fi = 0; fi < g->function_count; fi++) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                for (int k = 0; k < argc; k++) {
                                    call_args[k] = emit_expr(g, expr->call.args.items[k], locals);
                                }
                                LLVMValueRef result = LLVMBuildCall2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, call_args, (unsigned)argc, "scall");
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }
            }
        }

        /* generic function call — fallback */
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }

    case AST_STRING_INTERP: {
        /* String interpolation: convert each part to i8*, then concatenate.
         * Parts alternate: string_literal, expr, string_literal, expr, ... string_literal
         * Strategy: for each part, get an i8* string. Then compute total length,
         * malloc a buffer, strcpy+strcat all parts, return the buffer pointer. */
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);

        int n = expr->string_interp.parts.count;
        if (n == 0) return LLVMBuildGlobalStringPtr(g->builder, "", "empty");

        /* convert each part to i8* */
        LLVMValueRef *strs = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
        LLVMValueRef *lens = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));

        LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMTypeRef snprintf_type = LLVMFunctionType(
            LLVMInt32TypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1);

        for (int i = 0; i < n; i++) {
            zan_ast_node_t *part = expr->string_interp.parts.items[i];
            if (part->kind == AST_STRING_LITERAL) {
                strs[i] = LLVMBuildGlobalStringPtr(g->builder, part->str_val.str, "seg");
                lens[i] = LLVMConstInt(i64, (uint64_t)part->str_val.len, 0);
            } else {
                LLVMValueRef val = emit_expr(g, part, locals);
                LLVMTypeRef vt = LLVMTypeOf(val);
                LLVMTypeKind vtk = LLVMGetTypeKind(vt);

                if (vtk == LLVMPointerTypeKind) {
                    /* already a string */
                    strs[i] = val;
                    lens[i] = LLVMBuildCall2(g->builder, strlen_type, g->fn_strlen, &val, 1, "len");
                } else if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
                    /* snprintf(NULL, 0, "%g", val) to get length, then snprintf into buffer */
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "dfmt");
                    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, val };
                    LLVMValueRef needed = LLVMBuildCall2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                    LLVMValueRef buf_size = LLVMBuildAdd(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
                    LLVMValueRef buf = LLVMBuildCall2(g->builder, malloc_type, g->fn_malloc, &buf_size, 1, "buf");
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, val };
                    LLVMBuildCall2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
                } else {
                    /* integer types — format with %lld */
                    LLVMValueRef val64 = val;
                    if (LLVMGetIntTypeWidth(vt) < 64) {
                        val64 = LLVMBuildSExt(g->builder, val, i64, "ext");
                    }
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld", "ifmt");
                    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, val64 };
                    LLVMValueRef needed = LLVMBuildCall2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                    LLVMValueRef buf_size = LLVMBuildAdd(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
                    LLVMValueRef buf = LLVMBuildCall2(g->builder, malloc_type, g->fn_malloc, &buf_size, 1, "buf");
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, val64 };
                    LLVMBuildCall2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
                }
            }
        }

        /* compute total length */
        LLVMValueRef total_len = LLVMConstInt(i64, 0, 0);
        for (int i = 0; i < n; i++) {
            total_len = LLVMBuildAdd(g->builder, total_len, lens[i], "tlen");
        }
        LLVMValueRef alloc_size = LLVMBuildAdd(g->builder, total_len, LLVMConstInt(i64, 1, 0), "asz");

        /* malloc result buffer */
        LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
        LLVMValueRef result = LLVMBuildCall2(g->builder, malloc_type, g->fn_malloc, &alloc_size, 1, "interp");

        /* strcpy first, strcat rest */
        LLVMTypeRef strcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef strcpy_args[] = { result, strs[0] };
        LLVMBuildCall2(g->builder, strcpy_type, g->fn_strcpy, strcpy_args, 2, "");

        for (int i = 1; i < n; i++) {
            LLVMValueRef cat_args[] = { result, strs[i] };
            LLVMBuildCall2(g->builder, strcpy_type, g->fn_strcat, cat_args, 2, "");
        }

        free(strs);
        free(lens);
        return result;
    }

    case AST_MEMBER_ACCESS: {
        /* Math.PI → constant */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_istr_t obj = expr->member.object->ident.name;
            if (obj.len == 4 && memcmp(obj.str, "Math", 4) == 0) {
                if (expr->member.name.len == 2 && memcmp(expr->member.name.str, "PI", 2) == 0) {
                    return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), 3.14159265358979323846);
                }
                if (expr->member.name.len == 4 && memcmp(expr->member.name.str, "Sqrt", 4) == 0) {
                    /* Math.Sqrt is handled as a call — shouldn't reach here */
                    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* struct field access: obj.Field */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->member.object->ident.name);
            if (local && local->type && (local->type->kind == TYPE_STRUCT || local->type->kind == TYPE_CLASS)) {
                zan_symbol_t *type_sym = local->type->sym;
                if (type_sym) {
                    int fi = get_field_index(type_sym, expr->member.name);
                    if (fi >= 0) {
                        LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
                        if (st) {
                            /* load struct pointer or value, then GEP to field */
                            LLVMValueRef struct_ptr = local->alloca;
                            LLVMValueRef field_ptr = LLVMBuildStructGEP2(g->builder, st, struct_ptr, (unsigned)fi, "fld");
                            zan_symbol_t *fsym = get_field_sym(type_sym, expr->member.name);
                            LLVMTypeRef field_type = fsym ? map_type(g, fsym->type) : LLVMInt64TypeInContext(g->ctx);
                            return LLVMBuildLoad2(g->builder, field_type, field_ptr, "fval");
                        }
                    }
                }
            }
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    }

    case AST_INDEX: {
        /* arr[i] — array element access */
        LLVMValueRef arr_ptr = NULL;
        zan_type_t *arr_type = NULL;
        if (expr->index.object->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->index.object->ident.name);
            if (local) {
                arr_ptr = LLVMBuildLoad2(g->builder, LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                    local->alloca, "arrload");
                arr_type = local->type;
            }
        }
        if (arr_ptr && arr_type) {
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
            if (arr_type->element_type) {
                elem_llvm = map_type(g, arr_type->element_type);
            }
            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, arr_ptr, &idx, 1, "eidx");
            return LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "elem");
        }
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }

    case AST_NEW_EXPR: {
        /* new Type[size] — array creation */
        if (expr->new_expr.is_array && expr->new_expr.args.count > 0) {
            zan_type_t *elem_type = zan_binder_resolve_type(g->binder, expr->new_expr.type);
            if (!elem_type) elem_type = g->binder->type_int;
            LLVMTypeRef elem_llvm = map_type(g, elem_type);
            LLVMValueRef size_val = emit_expr(g, expr->new_expr.args.items[0], locals);

            /* malloc(size * sizeof(elem)) */
            LLVMValueRef elem_size = LLVMSizeOf(elem_llvm);
            LLVMValueRef total = LLVMBuildMul(g->builder,
                LLVMBuildZExt(g->builder, size_val, LLVMInt64TypeInContext(g->ctx), "zext"),
                elem_size, "total");
            LLVMTypeRef malloc_type = LLVMFunctionType(
                LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                (LLVMTypeRef[]){LLVMInt64TypeInContext(g->ctx)}, 1, 0);
            LLVMValueRef arr = LLVMBuildCall2(g->builder, malloc_type, g->fn_malloc, &total, 1, "arr");
            return arr;
        }

        /* new ClassName(args) — allocate struct + call constructor */
        zan_istr_t type_name = {NULL, 0};
        if (expr->new_expr.type) {
            if (expr->new_expr.type->kind == AST_IDENTIFIER) {
                type_name = expr->new_expr.type->ident.name;
            } else if (expr->new_expr.type->kind == AST_TYPE_REF) {
                type_name = expr->new_expr.type->type_ref.name;
            }
        }
        if (type_name.str) {
            zan_symbol_t *sym = zan_binder_lookup(g->binder, type_name);
            if (sym && sym->type && (sym->type->kind == TYPE_STRUCT || sym->type->kind == TYPE_CLASS)) {
                LLVMTypeRef st = get_struct_llvm_type(g, sym);
                if (st) {
                    LLVMValueRef alloca = LLVMBuildAlloca(g->builder, st, "new");
                    LLVMBuildStore(g->builder, LLVMConstNull(st), alloca);

                    /* look for constructor */
                    for (int ci = 0; ci < g->ctor_count; ci++) {
                        if (g->ctors[ci].type_sym == sym) {
                            int argc = expr->new_expr.args.count + 1;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                            call_args[0] = alloca; /* this ptr */
                            for (int k = 0; k < expr->new_expr.args.count; k++) {
                                call_args[k + 1] = emit_expr(g, expr->new_expr.args.items[k], locals);
                            }
                            LLVMBuildCall2(g->builder, g->ctors[ci].fn_type,
                                g->ctors[ci].fn, call_args, (unsigned)argc, "");
                            free(call_args);
                            break;
                        }
                    }

                    /* if no constructor, handle field initializers */
                    if (g->ctor_count == 0 || 1) {
                        for (int i = 0; i < expr->new_expr.args.count; i++) {
                            zan_ast_node_t *arg = expr->new_expr.args.items[i];
                            if (arg->kind == AST_ASSIGNMENT && arg->binary.left->kind == AST_IDENTIFIER) {
                                int fi = get_field_index(sym, arg->binary.left->ident.name);
                                if (fi >= 0) {
                                    LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, alloca, (unsigned)fi, "finit");
                                    LLVMValueRef fval = emit_expr(g, arg->binary.right, locals);
                                    zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                    if (fsym && fsym->type) {
                                        LLVMTypeRef target_t = map_type(g, fsym->type);
                                        LLVMTypeRef val_t = LLVMTypeOf(fval);
                                        if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                            LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                            fval = LLVMBuildFPTrunc(g->builder, fval, target_t, "trunc");
                                        }
                                    }
                                    LLVMBuildStore(g->builder, fval, fptr);
                                }
                            }
                        }
                    }
                    return alloca;
                }
            }
        }
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }

    default:
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }
}

/* ---- statement codegen ---- */

static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals) {
    if (!stmt) return;

    switch (stmt->kind) {
    case AST_BLOCK:
        for (int i = 0; i < stmt->block.stmts.count; i++) {
            emit_stmt(g, stmt->block.stmts.items[i], locals);
        }
        break;

    case AST_VAR_DECL: {
        zan_type_t *type = g->binder->type_int; /* default */
        if (stmt->var_decl.type) {
            type = zan_binder_resolve_type(g->binder, stmt->var_decl.type);
        } else if (stmt->var_decl.initializer) {
            zan_ast_node_t *init = stmt->var_decl.initializer;

            /* check if initializer is array creation: new int[5] */
            if (init->kind == AST_NEW_EXPR && init->new_expr.is_array) {
                LLVMValueRef arr_val = emit_expr(g, init, locals);
                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef alloca = LLVMBuildAlloca(g->builder, ptr_type, "arr");
                LLVMBuildStore(g->builder, arr_val, alloca);

                zan_type_t *elem_type = zan_binder_resolve_type(g->binder, init->new_expr.type);
                if (!elem_type) elem_type = g->binder->type_int;
                zan_type_t *arr_type = (zan_type_t *)zan_arena_alloc(g->arena, sizeof(zan_type_t));
                arr_type->kind = TYPE_ARRAY;
                arr_type->element_type = elem_type;
                arr_type->name = (zan_istr_t){NULL, 0};
                arr_type->sym = NULL;
                arr_type->type_args = NULL;
                arr_type->type_arg_count = 0;
                local_add(locals, stmt->var_decl.name, alloca, arr_type);
                return;
            }

            /* check if initializer is a struct construction: Point { X = 3.0, Y = 4.0 } */
            if (init->kind == AST_NEW_EXPR && init->new_expr.type) {
                zan_istr_t type_name = {NULL, 0};
                if (init->new_expr.type->kind == AST_IDENTIFIER) {
                    type_name = init->new_expr.type->ident.name;
                } else if (init->new_expr.type->kind == AST_TYPE_REF) {
                    type_name = init->new_expr.type->type_ref.name;
                }
                if (type_name.str) {
                    zan_symbol_t *sym = zan_binder_lookup(g->binder, type_name);
                    if (sym && sym->type && (sym->type->kind == TYPE_STRUCT || sym->type->kind == TYPE_CLASS)) {
                        type = sym->type;
                        LLVMTypeRef st = get_struct_llvm_type(g, sym);
                        if (st) {
                            LLVMValueRef alloca = LLVMBuildAlloca(g->builder, st, "var");
                            LLVMBuildStore(g->builder, LLVMConstNull(st), alloca);

                            /* try calling constructor */
                            bool ctor_called = false;
                            for (int ci = 0; ci < g->ctor_count; ci++) {
                                if (g->ctors[ci].type_sym == sym &&
                                    g->ctors[ci].param_count == init->new_expr.args.count) {
                                    int argc = init->new_expr.args.count + 1;
                                    LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                                    call_args[0] = alloca;
                                    for (int k = 0; k < init->new_expr.args.count; k++) {
                                        call_args[k + 1] = emit_expr(g, init->new_expr.args.items[k], locals);
                                    }
                                    LLVMBuildCall2(g->builder, g->ctors[ci].fn_type,
                                        g->ctors[ci].fn, call_args, (unsigned)argc, "");
                                    free(call_args);
                                    ctor_called = true;
                                    break;
                                }
                            }

                            /* if no constructor, handle field initializers */
                            if (!ctor_called) {
                                for (int i = 0; i < init->new_expr.args.count; i++) {
                                    zan_ast_node_t *arg = init->new_expr.args.items[i];
                                    if (arg->kind == AST_ASSIGNMENT && arg->binary.left->kind == AST_IDENTIFIER) {
                                        int fi = get_field_index(sym, arg->binary.left->ident.name);
                                        if (fi >= 0) {
                                            LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, alloca, (unsigned)fi, "finit");
                                            LLVMValueRef fval = emit_expr(g, arg->binary.right, locals);
                                            zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                            if (fsym && fsym->type) {
                                                LLVMTypeRef target_t = map_type(g, fsym->type);
                                                LLVMTypeRef val_t = LLVMTypeOf(fval);
                                                if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                                    fval = LLVMBuildFPTrunc(g->builder, fval, target_t, "trunc");
                                                } else if (LLVMGetTypeKind(target_t) == LLVMDoubleTypeKind &&
                                                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                                                    fval = LLVMBuildFPExt(g->builder, fval, target_t, "ext");
                                                }
                                            }
                                            LLVMBuildStore(g->builder, fval, fptr);
                                        }
                                    }
                                }
                            }
                            local_add(locals, stmt->var_decl.name, alloca, type);
                            return;
                        }
                    }
                }
            }

            /* regular type inference from initializer */
            LLVMValueRef init_val = emit_expr(g, init, locals);
            LLVMTypeRef init_type = LLVMTypeOf(init_val);
            LLVMValueRef alloca = LLVMBuildAlloca(g->builder, init_type, "var");
            LLVMBuildStore(g->builder, init_val, alloca);
            if (LLVMGetTypeKind(init_type) == LLVMDoubleTypeKind) {
                type = g->binder->type_double;
            } else if (LLVMGetTypeKind(init_type) == LLVMFloatTypeKind) {
                type = g->binder->type_float;
            } else if (LLVMGetTypeKind(init_type) == LLVMPointerTypeKind) {
                type = g->binder->type_string;
            } else if (LLVMGetTypeKind(init_type) == LLVMIntegerTypeKind) {
                unsigned bits = LLVMGetIntTypeWidth(init_type);
                if (bits <= 32) type = g->binder->type_int;
                else type = g->binder->type_long;
            } else {
                type = g->binder->type_int;
            }
            local_add(locals, stmt->var_decl.name, alloca, type);
            return;
        }

        LLVMTypeRef llvm_type = map_type(g, type);
        LLVMValueRef alloca = LLVMBuildAlloca(g->builder, llvm_type, "var");

        if (stmt->var_decl.initializer) {
            LLVMValueRef init_val = emit_expr(g, stmt->var_decl.initializer, locals);
            LLVMBuildStore(g->builder, init_val, alloca);
        }

        local_add(locals, stmt->var_decl.name, alloca, type);
        break;
    }

    case AST_EXPR_STMT:
        emit_expr(g, stmt->expr_stmt.expr, locals);
        break;

    case AST_RETURN_STMT:
        if (stmt->ret.value) {
            LLVMValueRef val = emit_expr(g, stmt->ret.value, locals);
            /* convert return value to match function return type */
            LLVMTypeRef fn_ret = g->current_fn_ret_type;
            LLVMTypeRef val_t = LLVMTypeOf(val);
            if (val_t != fn_ret) {
                if (LLVMGetTypeKind(fn_ret) == LLVMFloatTypeKind &&
                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                    val = LLVMBuildFPTrunc(g->builder, val, fn_ret, "rettrunc");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMDoubleTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                    val = LLVMBuildFPExt(g->builder, val, fn_ret, "retext");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMIntegerTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind) {
                    unsigned fn_bits = LLVMGetIntTypeWidth(fn_ret);
                    unsigned val_bits = LLVMGetIntTypeWidth(val_t);
                    if (fn_bits > val_bits) {
                        val = LLVMBuildSExt(g->builder, val, fn_ret, "retext");
                    } else if (fn_bits < val_bits) {
                        val = LLVMBuildTrunc(g->builder, val, fn_ret, "rettrunc");
                    }
                }
            }
            LLVMBuildRet(g->builder, val);
        } else {
            LLVMBuildRetVoid(g->builder);
        }
        break;

    case AST_IF_STMT: {
        LLVMValueRef cond = emit_expr(g, stmt->if_stmt.cond, locals);
        /* ensure cond is i1 */
        if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind ||
            LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
            cond = LLVMBuildICmp(g->builder, LLVMIntNE, cond,
                                 LLVMConstInt(LLVMTypeOf(cond), 0, 0), "tobool");
        }

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "then");
        LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "else");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "merge");

        LLVMBuildCondBr(g->builder, cond, then_bb, stmt->if_stmt.else_body ? else_bb : merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, then_bb);
        emit_stmt(g, stmt->if_stmt.then_body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            LLVMBuildBr(g->builder, merge_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, else_bb);
        if (stmt->if_stmt.else_body) {
            emit_stmt(g, stmt->if_stmt.else_body, locals);
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            LLVMBuildBr(g->builder, merge_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        break;
    }

    case AST_WHILE_STMT: {
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.end");

        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        g->break_target = end_bb;
        g->continue_target = cond_bb;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef cond = emit_expr(g, stmt->while_stmt.cond, locals);
        if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind ||
            LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
            cond = LLVMBuildICmp(g->builder, LLVMIntNE, cond,
                                 LLVMConstInt(LLVMTypeOf(cond), 0, 0), "tobool");
        }
        LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->while_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            LLVMBuildBr(g->builder, cond_bb);
        }

        g->break_target = saved_break;
        g->continue_target = saved_cont;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_FOR_STMT: {
        if (stmt->for_stmt.init) emit_stmt(g, stmt->for_stmt.init, locals);

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.body");
        LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.step");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.end");

        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        g->break_target = end_bb;
        g->continue_target = step_bb;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        if (stmt->for_stmt.cond) {
            LLVMValueRef cond = emit_expr(g, stmt->for_stmt.cond, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
                cond = LLVMBuildICmp(g->builder, LLVMIntNE, cond,
                                     LLVMConstInt(LLVMTypeOf(cond), 0, 0), "tobool");
            }
            LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);
        } else {
            LLVMBuildBr(g->builder, body_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->for_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            LLVMBuildBr(g->builder, step_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, step_bb);
        if (stmt->for_stmt.step) emit_expr(g, stmt->for_stmt.step, locals);
        LLVMBuildBr(g->builder, cond_bb);

        g->break_target = saved_break;
        g->continue_target = saved_cont;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_BREAK_STMT:
        if (g->break_target) {
            LLVMBuildBr(g->builder, g->break_target);
        }
        break;

    case AST_CONTINUE_STMT:
        if (g->continue_target) {
            LLVMBuildBr(g->builder, g->continue_target);
        }
        break;

    case AST_SWITCH_STMT: {
        LLVMValueRef switch_val = emit_expr(g, stmt->switch_stmt.expr, locals);
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.end");

        /* count non-default cases */
        int num_cases = 0;
        zan_ast_node_t *default_case = NULL;
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.pattern) {
                num_cases++;
            } else {
                default_case = sc;
            }
        }

        LLVMBasicBlockRef default_bb = default_case
            ? LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.default")
            : end_bb;

        LLVMValueRef sw = LLVMBuildSwitch(g->builder, switch_val, default_bb, (unsigned)num_cases);

        /* emit each case block */
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (!sc->switch_case.pattern) continue; /* default handled separately */

            LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.case");
            LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
            LLVMAddCase(sw, case_val, case_bb);

            LLVMPositionBuilderAtEnd(g->builder, case_bb);
            emit_stmt(g, sc->switch_case.body, locals);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                LLVMBuildBr(g->builder, end_bb);
            }
        }

        /* emit default block */
        if (default_case) {
            LLVMPositionBuilderAtEnd(g->builder, default_bb);
            emit_stmt(g, default_case->switch_case.body, locals);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                LLVMBuildBr(g->builder, end_bb);
            }
        }

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_TRY_STMT: {
        /* M2 basic try/catch: emit try body, skip catch/finally for now
         * (full exception handling requires personality function + landingpad) */
        emit_stmt(g, stmt->try_stmt.try_body, locals);
        /* TODO: M3+ full LLVM exception handling with landingpad */
        break;
    }

    default:
        break;
    }
}

/* ---- top-level emission ---- */

static void emit_main_method(zan_irgen_t *g, zan_ast_node_t *method) {
    /* create main() function */
    LLVMTypeRef main_type = LLVMFunctionType(
        LLVMInt32TypeInContext(g->ctx), NULL, 0, 0);
    LLVMValueRef main_fn = LLVMAddFunction(g->mod, "main", main_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, main_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    g->current_fn = main_fn;
    g->current_fn_ret_type = LLVMInt32TypeInContext(g->ctx);

    local_scope_t *locals = local_scope_new(g->arena);

    if (method->method_decl.body) {
        emit_stmt(g, method->method_decl.body, locals);
    }

    /* add return 0 if no terminator */
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0));
    }
}

/* ---- emit user-defined methods ---- */

static void emit_user_methods(zan_irgen_t *g, zan_ast_node_t *unit) {
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;

        /* look up symbol for this type */
        zan_symbol_t *type_sym = zan_binder_lookup(g->binder, decl->type_decl.name);
        if (!type_sym) continue;

        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            bool is_ctor = (member->kind == AST_CONSTRUCTOR_DECL);
            if (member->kind != AST_METHOD_DECL && !is_ctor) continue;
            if (!member->method_decl.body) continue;

            /* skip static Main — handled separately */
            bool is_static = !is_ctor && (member->method_decl.modifiers & MOD_STATIC) != 0;
            if (is_static && member->method_decl.name.len == 4 &&
                memcmp(member->method_decl.name.str, "Main", 4) == 0) continue;

            /* build function name: TypeName_MethodName or TypeName_ctor */
            char fn_name[512];
            if (is_ctor) {
                snprintf(fn_name, sizeof(fn_name), "%.*s_ctor",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str);
            } else {
                snprintf(fn_name, sizeof(fn_name), "%.*s_%.*s",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str,
                         (int)member->method_decl.name.len, member->method_decl.name.str);
            }

            /* build function type */
            int param_count = member->method_decl.params.count;
            int total_params = is_static ? param_count : param_count + 1;
            LLVMTypeRef *param_types = (LLVMTypeRef *)calloc((size_t)(total_params > 0 ? total_params : 1), sizeof(LLVMTypeRef));

            int param_offset = 0;
            LLVMValueRef this_alloca = NULL;
            if (!is_static) {
                /* this pointer for instance methods */
                LLVMTypeRef struct_type = get_struct_llvm_type(g, type_sym);
                param_types[0] = struct_type ? LLVMPointerType(struct_type, 0)
                                             : LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                param_offset = 1;
            }

            for (int k = 0; k < param_count; k++) {
                zan_ast_node_t *param = member->method_decl.params.items[k];
                zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
                param_types[k + param_offset] = map_type(g, pt);
            }

            zan_type_t *ret_type = is_ctor ? g->binder->type_void
                : (member->method_decl.return_type
                    ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                    : g->binder->type_void);
            LLVMTypeRef llvm_ret = map_type(g, ret_type);
            LLVMTypeRef fn_type = LLVMFunctionType(llvm_ret, param_types, (unsigned)total_params, 0);
            LLVMValueRef fn = LLVMAddFunction(g->mod, fn_name, fn_type);

            /* register in function/ctor table */
            if (is_ctor) {
                if (g->ctor_count < 256) {
                    g->ctors[g->ctor_count].type_sym = type_sym;
                    g->ctors[g->ctor_count].fn = fn;
                    g->ctors[g->ctor_count].fn_type = fn_type;
                    g->ctors[g->ctor_count].param_count = param_count;
                    g->ctor_count++;
                }
            } else if (g->function_count < 1024) {
                zan_symbol_t *method_sym = get_method_sym(type_sym, member->method_decl.name);
                g->functions[g->function_count].sym = method_sym;
                g->functions[g->function_count].fn = fn;
                g->functions[g->function_count].fn_type = fn_type;
                g->function_count++;
            }

            /* emit body */
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
            LLVMPositionBuilderAtEnd(g->builder, entry);

            local_scope_t *locals = local_scope_new(g->arena);

            if (!is_static) {
                /* bind 'this' as first parameter */
                this_alloca = LLVMBuildAlloca(g->builder, param_types[0], "this");
                LLVMBuildStore(g->builder, LLVMGetParam(fn, 0), this_alloca);
            }

            /* bind method parameters */
            for (int k = 0; k < param_count; k++) {
                zan_ast_node_t *param = member->method_decl.params.items[k];
                LLVMValueRef param_alloca = LLVMBuildAlloca(g->builder, param_types[k + param_offset], "p");
                LLVMBuildStore(g->builder, LLVMGetParam(fn, (unsigned)(k + param_offset)), param_alloca);
                zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
                local_add(locals, param->param.name, param_alloca, pt);
            }

            LLVMValueRef saved_fn = g->current_fn;
            LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
            LLVMValueRef saved_this = g->current_this;
            zan_symbol_t *saved_type_sym = g->current_type_sym;
            g->current_fn = fn;
            g->current_fn_ret_type = llvm_ret;
            g->current_this = is_static ? NULL : this_alloca;
            g->current_type_sym = is_static ? NULL : type_sym;

            /* emit method body */
            if (member->method_decl.body->kind == AST_BLOCK) {
                for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
                    emit_stmt(g, member->method_decl.body->block.stmts.items[k], locals);
                }
            } else {
                /* expression body (=> expr) */
                LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
                /* convert return type if needed */
                LLVMTypeRef val_t = LLVMTypeOf(val);
                if (val_t != llvm_ret) {
                    if (LLVMGetTypeKind(llvm_ret) == LLVMFloatTypeKind &&
                        LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                        val = LLVMBuildFPTrunc(g->builder, val, llvm_ret, "trunc");
                    } else if (LLVMGetTypeKind(llvm_ret) == LLVMDoubleTypeKind &&
                               LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                        val = LLVMBuildFPExt(g->builder, val, llvm_ret, "ext");
                    }
                }
                LLVMBuildRet(g->builder, val);
            }

            /* ensure function has terminator */
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            if (!LLVMGetBasicBlockTerminator(cur_bb)) {
                if (ret_type->kind == TYPE_VOID) {
                    LLVMBuildRetVoid(g->builder);
                } else {
                    LLVMBuildRet(g->builder, LLVMConstNull(llvm_ret));
                }
            }

            g->current_fn = saved_fn;
            g->current_fn_ret_type = saved_fn_ret;
            g->current_this = saved_this;
            g->current_type_sym = saved_type_sym;
            free(param_types);
        }
    }
}

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return ZAN_ERROR;

    /* Pass 1: register all struct/class types */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_STRUCT_DECL || decl->kind == AST_CLASS_DECL) {
            zan_symbol_t *sym = zan_binder_lookup(g->binder, decl->type_decl.name);
            if (sym) register_struct_type(g, sym);
        }
    }

    /* Pass 2: emit user-defined methods */
    emit_user_methods(g, unit);

    /* Pass 3: find and emit static Main method */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;

        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            if (member->kind == AST_METHOD_DECL &&
                (member->method_decl.modifiers & MOD_STATIC) &&
                member->method_decl.name.len == 4 &&
                memcmp(member->method_decl.name.str, "Main", 4) == 0) {
                emit_main_method(g, member);
                goto done;
            }
        }
    }
done:
    ;
    /* verify module */
    char *error = NULL;
    if (LLVMVerifyModule(g->mod, LLVMReturnStatusAction, &error)) {
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "LLVM verification failed: %s", error);
        LLVMDisposeMessage(error);
        return ZAN_ERROR;
    }
    if (error) LLVMDisposeMessage(error);

    return ZAN_OK;
}

/* ---- output ---- */

zan_status_t zan_irgen_write_ir(zan_irgen_t *g, const char *path) {
    char *ir = LLVMPrintModuleToString(g->mod);
    if (!path) {
        printf("%s", ir);
        LLVMDisposeMessage(ir);
        return ZAN_OK;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        LLVMDisposeMessage(ir);
        return ZAN_ERROR;
    }
    fputs(ir, f);
    fclose(f);
    LLVMDisposeMessage(ir);
    return ZAN_OK;
}

zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path) {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();

    char *triple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;
    char *error = NULL;

    if (LLVMGetTargetFromTriple(triple, &target, &error)) {
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "failed to get target: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(triple);
        return ZAN_ERROR;
    }

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, "generic", "",
        LLVMCodeGenLevelDefault, LLVMRelocPIC, LLVMCodeModelDefault);

    LLVMSetTarget(g->mod, triple);
    LLVMTargetDataRef dl = LLVMCreateTargetDataLayout(tm);
    char *dl_str = LLVMCopyStringRepOfTargetData(dl);
    LLVMSetDataLayout(g->mod, dl_str);
    LLVMDisposeMessage(dl_str);
    LLVMDisposeTargetData(dl);

    if (LLVMTargetMachineEmitToFile(tm, g->mod, (char *)path,
                                     LLVMObjectFile, &error)) {
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "failed to emit object file: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(tm);
        LLVMDisposeMessage(triple);
        return ZAN_ERROR;
    }

    LLVMDisposeTargetMachine(tm);
    LLVMDisposeMessage(triple);
    return ZAN_OK;
}
