/* opt_tbaa.c -- type-based alias metadata for object/struct field accesses.
 *
 * Without it LLVM must assume a store to `a.vx` may change `b.m`, so every
 * field is reloaded after every store (nbody: 0.8 s vs C's 0.4 s, and C
 * with -fno-strict-aliasing is exactly as slow as Zan was).
 *
 * The tag of a scalar field access is its byte offset inside the struct it
 * is declared in. That is sound for Zan's memory model: distinct objects
 * never overlap; a derived class extends its base's layout, so the same
 * object seen through either type has the same field at the same offset;
 * scalar fields of one struct never overlap (explicit-layout structs, the
 * only exception, are not tagged). Interior accesses into embedded structs
 * use offsets relative to that struct -- equal tags for unrelated memory are
 * merely conservative. Aggregate-typed fields, accesses whose type differs
 * from the field type, and every access not made through emit_field_ptr
 * stay untagged and so alias everything.
 *
 * emit_field_ptr marks the field address (`!zan.fo`); this pass, run before
 * optimization, moves that onto each load/store that uses the address
 * directly. ZAN_NO_TBAA=1 disables it (A/B). */
#include "irgen.h"
#include "optimizer.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZAN_FO_KIND "zan.fo"

static bool tbaa_scalar(LLVMTypeRef t) {
    switch (LLVMGetTypeKind(t)) {
        case LLVMIntegerTypeKind:
        case LLVMFloatTypeKind:
        case LLVMDoubleTypeKind:
        case LLVMPointerTypeKind: return true;
        default: return false;
    }
}

void zan_tbaa_mark_field(zan_irgen_t *g, LLVMValueRef gep, LLVMTypeRef st, int fi) {
    if (!gep || !LLVMIsAGetElementPtrInst(gep) || fi < 0) return;
    if (LLVMGetTypeKind(st) != LLVMStructTypeKind || LLVMIsOpaqueStruct(st)) return;
    if ((unsigned)fi >= LLVMCountStructElementTypes(st)) return;
    if (!tbaa_scalar(LLVMStructGetTypeAtIndex(st, (unsigned)fi))) return;
    if (!g->tbaa_td) g->tbaa_td = LLVMCreateTargetData(LLVMGetDataLayoutStr(g->mod));
    unsigned long long off = LLVMOffsetOfElement(g->tbaa_td, st, (unsigned)fi);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMMetadataRef node = LLVMValueAsMetadata(LLVMConstInt(i64, off, 0));
    LLVMMetadataRef md = LLVMMDNodeInContext2(g->ctx, &node, 1);
    unsigned kind = LLVMGetMDKindIDInContext(g->ctx, ZAN_FO_KIND, (unsigned)strlen(ZAN_FO_KIND));
    LLVMSetMetadata(gep, kind, LLVMMetadataAsValue(g->ctx, md));
}

/* The array length word (base-16) gets its own tag. Only array
 * headers live there -- objects and arrays never overlap and a field access
 * never addresses an array header -- so field stores no longer force a
 * reload of the length, and a bounds check's length load can leave the loop
 * (a store to b.vx in nbody's inner loop used to re-read bs.Length). */
void zan_tbaa_tag_array_len(zan_irgen_t *g, LLVMValueRef load) {
    if (!load || !LLVMIsALoadInst(load) || getenv("ZAN_NO_TBAA")) return;
    LLVMContextRef ctx = g->ctx;
    LLVMMetadataRef root_name = LLVMMDStringInContext2(ctx, "Zan TBAA", 8);
    LLVMMetadataRef root = LLVMMDNodeInContext2(ctx, &root_name, 1);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMMetadataRef zero = LLVMValueAsMetadata(LLVMConstInt(i64, 0, 0));
    LLVMMetadataRef ty_ops[3] = {LLVMMDStringInContext2(ctx, "zan.arraylen", 12), root, zero};
    LLVMMetadataRef ty = LLVMMDNodeInContext2(ctx, ty_ops, 3);
    LLVMMetadataRef tag_ops[3] = {ty, ty, zero};
    LLVMSetMetadata(load, LLVMGetMDKindIDInContext(ctx, "tbaa", 4),
                    LLVMMetadataAsValue(ctx, LLVMMDNodeInContext2(ctx, tag_ops, 3)));
}

typedef struct {
    unsigned long long off;
    LLVMValueRef tag;
} tbaa_entry_t;

static LLVMValueRef tbaa_tag(LLVMContextRef ctx, LLVMMetadataRef root, tbaa_entry_t **tab, int *n,
                             int *cap, unsigned long long off) {
    for (int i = 0; i < *n; i++)
        if ((*tab)[i].off == off) return (*tab)[i].tag;
    char name[48];
    snprintf(name, sizeof name, "zan.field+%llu", off);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMMetadataRef zero = LLVMValueAsMetadata(LLVMConstInt(i64, 0, 0));
    LLVMMetadataRef ty_ops[3] = {LLVMMDStringInContext2(ctx, name, strlen(name)), root, zero};
    LLVMMetadataRef ty = LLVMMDNodeInContext2(ctx, ty_ops, 3);
    LLVMMetadataRef tag_ops[3] = {ty, ty, zero};
    LLVMValueRef tag = LLVMMetadataAsValue(ctx, LLVMMDNodeInContext2(ctx, tag_ops, 3));
    if (*n == *cap) {
        int nc = *cap ? *cap * 2 : 32;
        tbaa_entry_t *nt = realloc(*tab, (size_t)nc * sizeof **tab);
        if (!nt) return NULL;
        *tab = nt;
        *cap = nc;
    }
    (*tab)[(*n)++] = (tbaa_entry_t){off, tag};
    return tag;
}

int zan_opt_tbaa(LLVMModuleRef mod) {
    if (getenv("ZAN_NO_TBAA")) return 0;
    LLVMContextRef ctx = LLVMGetModuleContext(mod);
    unsigned k_fo = LLVMGetMDKindIDInContext(ctx, ZAN_FO_KIND, (unsigned)strlen(ZAN_FO_KIND));
    unsigned k_tbaa = LLVMGetMDKindIDInContext(ctx, "tbaa", 4);
    LLVMMetadataRef root_name = LLVMMDStringInContext2(ctx, "Zan TBAA", 8);
    LLVMMetadataRef root = LLVMMDNodeInContext2(ctx, &root_name, 1);
    tbaa_entry_t *tab = NULL;
    int n = 0, cap = 0, tagged = 0;
    for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn; fn = LLVMGetNextFunction(fn)) {
        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
             bb = LLVMGetNextBasicBlock(bb)) {
            for (LLVMValueRef i = LLVMGetFirstInstruction(bb); i; i = LLVMGetNextInstruction(i)) {
                LLVMValueRef ptr, gep;
                LLVMTypeRef access;
                if (LLVMIsALoadInst(i)) {
                    ptr = LLVMGetOperand(i, 0);
                    access = LLVMTypeOf(i);
                } else if (LLVMIsAStoreInst(i)) {
                    ptr = LLVMGetOperand(i, 1);
                    access = LLVMTypeOf(LLVMGetOperand(i, 0));
                } else {
                    continue;
                }
                gep = ptr;
                if (!LLVMIsAGetElementPtrInst(gep)) continue;
                LLVMValueRef fo = LLVMGetMetadata(gep, k_fo);
                if (!fo) continue;
                /* Field index is the last GEP operand: struct GEP (0, fi). */
                LLVMTypeRef src = LLVMGetGEPSourceElementType(gep);
                unsigned nops = (unsigned)LLVMGetNumOperands(gep);
                LLVMValueRef idx = nops >= 3 ? LLVMGetOperand(gep, nops - 1) : NULL;
                if (!idx || !LLVMIsAConstantInt(idx) || LLVMGetTypeKind(src) != LLVMStructTypeKind)
                    continue;
                unsigned fi = (unsigned)LLVMConstIntGetZExtValue(idx);
                if (fi >= LLVMCountStructElementTypes(src) ||
                    LLVMStructGetTypeAtIndex(src, fi) != access)
                    continue;
                LLVMValueRef offc = LLVMGetOperand(fo, 0);
                if (!offc || !LLVMIsAConstantInt(offc)) continue;
                LLVMValueRef tag =
                    tbaa_tag(ctx, root, &tab, &n, &cap, LLVMConstIntGetZExtValue(offc));
                if (!tag) continue;
                LLVMSetMetadata(i, k_tbaa, tag);
                tagged++;
            }
        }
    }
    free(tab);
    return tagged;
}
