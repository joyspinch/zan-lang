
#include "optimizer.h"
#include "irgen.h"
#include "binder.h"
#include "zan_abi.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <malloc.h>
#else
#include <alloca.h>
#endif

#ifdef _WIN32
#include <windows.h>
static double get_time_ms(void) {
  LARGE_INTEGER freq, count;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&count);
  return (double)count.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
#include <time.h>
static double get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
#endif

typedef enum {
  ARC_NONE = 0,
  ARC_RETAIN_OBJ,
  ARC_RELEASE_OBJ,
  ARC_RETAIN_STR,
  ARC_RELEASE_STR,
} zan_arc_call_kind_t;

static zan_arc_call_kind_t get_arc_call_kind(LLVMValueRef inst) {
  if (!inst || LLVMGetInstructionOpcode(inst) != LLVMCall)
    return ARC_NONE;
  LLVMValueRef callee = LLVMGetCalledValue(inst);
  if (!callee)
    return ARC_NONE;
  const char *fn_name = LLVMGetValueName(callee);
  if (!fn_name)
    return ARC_NONE;
  if (strcmp(fn_name, "zan_rt_retain") == 0 ||
      strcmp(fn_name, "zan_retain") == 0)
    return ARC_RETAIN_OBJ;
  if (strcmp(fn_name, "zan_rt_release") == 0 ||
      strcmp(fn_name, "zan_rt_release_dyn") == 0 ||
      strcmp(fn_name, "zan_release") == 0)
    return ARC_RELEASE_OBJ;
  if (strcmp(fn_name, "zan_rt_str_retain") == 0)
    return ARC_RETAIN_STR;
  if (strcmp(fn_name, "zan_rt_str_release") == 0)
    return ARC_RELEASE_STR;
  return ARC_NONE;
}

static LLVMValueRef strip_pointer_casts(LLVMValueRef v) {
  int depth = 0;
  while (v && depth < 16) {
    if (LLVMIsAInstruction(v)) {
      LLVMOpcode op = LLVMGetInstructionOpcode(v);
      if (op == LLVMBitCast || op == LLVMAddrSpaceCast) {
        v = LLVMGetOperand(v, 0);
        depth++;
        continue;
      }
    } else if (LLVMIsAConstantExpr(v)) {
      LLVMOpcode op = LLVMGetConstOpcode(v);
      if (op == LLVMBitCast || op == LLVMAddrSpaceCast) {
        v = LLVMGetOperand(v, 0);
        depth++;
        continue;
      }
    }
    break;
  }
  return v;
}

static bool gep_const_byte_offset(LLVMModuleRef mod, LLVMValueRef v,
                                  LLVMValueRef *out_base, uint64_t *out_off) {
  uint64_t total_off = 0;
  LLVMTargetDataRef td = LLVMGetModuleDataLayout(mod);
  if (!td)
    return false;
  for (int depth = 0; depth < 6; depth++) {
    v = strip_pointer_casts(v);
    if (!v)
      return false;
    bool is_gep = false;
    if (LLVMIsAInstruction(v) &&
        LLVMGetInstructionOpcode(v) == LLVMGetElementPtr) {
      is_gep = true;
    } else if (LLVMIsAConstantExpr(v) &&
               LLVMGetConstOpcode(v) == LLVMGetElementPtr) {
      is_gep = true;
    }
    if (!is_gep) {
      *out_base = v;
      *out_off = total_off;
      return depth > 0;
    }
    unsigned nops = (unsigned)LLVMGetNumOperands(v);
    if (nops < 2)
      return false;
    LLVMTypeRef cur_ty = LLVMGetGEPSourceElementType(v);
    if (!cur_ty)
      return false;
    for (unsigned i = 1; i < nops; i++) {
      LLVMValueRef idx_v = LLVMGetOperand(v, i);
      if (!idx_v || !LLVMIsAConstantInt(idx_v))
        return false;
      long long idx = LLVMConstIntGetSExtValue(idx_v);
      if (idx < 0)
        return false;
      if (i == 1) {
        total_off += (uint64_t)idx * LLVMABISizeOfType(td, cur_ty);
      } else {
        LLVMTypeKind tk = LLVMGetTypeKind(cur_ty);
        if (tk == LLVMStructTypeKind) {
          if ((unsigned)idx >= LLVMCountStructElementTypes(cur_ty))
            return false;
          total_off += LLVMOffsetOfElement(td, cur_ty, (unsigned)idx);
          cur_ty = LLVMStructGetTypeAtIndex(cur_ty, (unsigned)idx);
        } else if (tk == LLVMArrayTypeKind) {
          LLVMTypeRef elem_ty = LLVMGetElementType(cur_ty);
          total_off += (uint64_t)idx * LLVMABISizeOfType(td, elem_ty);
          cur_ty = elem_ty;
        } else {
          return false;
        }
      }
    }
    v = LLVMGetOperand(v, 0);
  }
  return false;
}

static bool is_immortal_arc_value(LLVMModuleRef mod, LLVMValueRef v,
                                  LLVMValueRef *visited, int nvis, int depth) {
  if (!v || depth > 8)
    return false;
  v = strip_pointer_casts(v);
  if (!v)
    return false;
  if (LLVMIsNull(v))
    return true;

  if (LLVMIsAInstruction(v)) {
    LLVMOpcode op = LLVMGetInstructionOpcode(v);
    if (op == LLVMPHI) {
      for (int i = 0; i < nvis; i++) {
        if (visited[i] == v)
          return true;
      }
      if (nvis >= 8)
        return false;
      unsigned n = LLVMCountIncoming(v);
      if (n == 0 || n > 16)
        return false;
      LLVMValueRef next_vis[8];
      for (int i = 0; i < nvis; i++)
        next_vis[i] = visited[i];
      next_vis[nvis] = v;
      for (unsigned i = 0; i < n; i++) {
        if (!is_immortal_arc_value(mod, LLVMGetIncomingValue(v, i), next_vis,
                                   nvis + 1, depth + 1))
          return false;
      }
      return true;
    }
    if (op == LLVMSelect && LLVMGetNumOperands(v) >= 3) {
      return is_immortal_arc_value(mod, LLVMGetOperand(v, 1), visited, nvis,
                                   depth + 1) &&
             is_immortal_arc_value(mod, LLVMGetOperand(v, 2), visited, nvis,
                                   depth + 1);
    }
  }

  LLVMValueRef base = NULL;
  uint64_t off = 0;
  if (!gep_const_byte_offset(mod, v, &base, &off))
    return false;
  if (!base || !LLVMIsAGlobalVariable(base) || off < 16)
    return false;
  LLVMValueRef init = LLVMGetInitializer(base);
  if (!init)
    return false;

  if (LLVMIsAConstantDataSequential(init)) {
    LLVMTypeRef it = LLVMTypeOf(init);
    LLVMTypeRef et = LLVMGetElementType(it);
    if (!et || LLVMGetTypeKind(et) != LLVMIntegerTypeKind ||
        LLVMGetIntTypeWidth(et) != 8)
      return false;
    const char *gname = LLVMGetValueName(base);
    if (!LLVMIsGlobalConstant(base) &&
        !(gname && strncmp(gname, "__zan.strlit.", 13) == 0))
      return false;
    size_t len = 0;
    const char *raw = LLVMGetAsString(init, &len);
    if (!raw || off > len)
      return false;
    for (int k = 0; k < 8; k++) {
      if ((uint8_t)raw[off - 16 + (size_t)k] != 0xFF)
        return false;
    }
    return true;
  }

  LLVMTypeRef st = LLVMTypeOf(init);
  if (LLVMGetTypeKind(st) == LLVMStructTypeKind && !LLVMIsPackedStruct(st) &&
      LLVMIsGlobalConstant(base)) {
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(mod);
    unsigned nf = LLVMCountStructElementTypes(st);
    for (unsigned f = 0; f < nf; f++) {
      if (LLVMOffsetOfElement(td, st, f) == off - 16) {
        LLVMTypeRef ft = LLVMStructGetTypeAtIndex(st, f);
        if (ft && LLVMGetTypeKind(ft) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(ft) == 64) {
          LLVMValueRef fv = LLVMGetAggregateElement(init, f);
          if (fv && LLVMIsAConstantInt(fv) &&
              LLVMConstIntGetSExtValue(fv) == -1LL)
            return true;
        }
        break;
      }
    }
  }

  return false;
}

static LLVMValueRef get_arc_operand(LLVMValueRef call) {
  if (!call || LLVMGetNumOperands(call) < 1)
    return NULL;
  return strip_pointer_casts(LLVMGetOperand(call, 0));
}

static bool are_same_arc_object(LLVMValueRef op1, LLVMValueRef op2) {
  op1 = strip_pointer_casts(op1);
  op2 = strip_pointer_casts(op2);
  /* Distinct loads of one slot can observe different objects after a callback
   * rewrites it. Only SSA identity proves retain/release cancellation safe. */
  return op1 && op1 == op2;
}

static bool is_safe_pure_function(const char *fn_name) {
  if (!fn_name)
    return false;

  /* LLVM 基础诊断与数值内联 */
  if (strncmp(fn_name, "llvm.lifetime.", 14) == 0 ||
      strncmp(fn_name, "llvm.dbg.", 9) == 0 ||
      strncmp(fn_name, "llvm.assume", 11) == 0 ||
      strncmp(fn_name, "llvm.expect", 11) == 0 ||
      strncmp(fn_name, "llvm.sadd.", 10) == 0 ||
      strncmp(fn_name, "llvm.uadd.", 10) == 0 ||
      strncmp(fn_name, "llvm.ssub.", 10) == 0 ||
      strncmp(fn_name, "llvm.usub.", 10) == 0 ||
      strncmp(fn_name, "llvm.smul.", 10) == 0 ||
      strncmp(fn_name, "llvm.umul.", 10) == 0 ||
      strncmp(fn_name, "llvm.smin.", 10) == 0 ||
      strncmp(fn_name, "llvm.smax.", 10) == 0 ||
      strncmp(fn_name, "llvm.umin.", 10) == 0 ||
      strncmp(fn_name, "llvm.umax.", 10) == 0 ||
      strncmp(fn_name, "llvm.abs.", 9) == 0 ||
      strncmp(fn_name, "llvm.bswap.", 11) == 0 ||
      strncmp(fn_name, "llvm.ctpop.", 11) == 0 ||
      strncmp(fn_name, "llvm.ctlz.", 10) == 0 ||
      strncmp(fn_name, "llvm.cttz.", 10) == 0 ||
      strncmp(fn_name, "llvm.sin.", 9) == 0 ||
      strncmp(fn_name, "llvm.cos.", 9) == 0 ||
      strncmp(fn_name, "llvm.sqrt.", 10) == 0 ||
      strncmp(fn_name, "llvm.fabs.", 10) == 0 ||
      strncmp(fn_name, "llvm.pow.", 9) == 0 ||
      strncmp(fn_name, "llvm.floor.", 11) == 0 ||
      strncmp(fn_name, "llvm.ceil.", 10) == 0 ||
      strncmp(fn_name, "llvm.round.", 11) == 0 ||
      strncmp(fn_name, "llvm.trunc.", 11) == 0 ||
      strncmp(fn_name, "llvm.minnum.", 12) == 0 ||
      strncmp(fn_name, "llvm.maxnum.", 12) == 0 ||
      strncmp(fn_name, "llvm.log.", 9) == 0 ||
      strncmp(fn_name, "llvm.exp.", 9) == 0) {
    return true;
  }

  /* 边界检查与只读运行时探针（不修改目标对象引用计数或内存布局） */
  if (strcmp(fn_name, "zan_bound_check") == 0 ||
      strcmp(fn_name, "zan_rt_bound_check") == 0 ||
      strcmp(fn_name, "zan_rt_str_len") == 0 ||
      strcmp(fn_name, "zan_rt_str_compare") == 0 ||
      strcmp(fn_name, "zan_rt_str_equal") == 0 ||
      strcmp(fn_name, "zan_rt_str_hash") == 0 ||
      strcmp(fn_name, "zan_rt_str_bytes") == 0 ||
      strcmp(fn_name, "zan_rt_arr_len") == 0 ||
      strcmp(fn_name, "zan_rt_now_ms") == 0 ||
      strcmp(fn_name, "zan_rt_tick_us") == 0) {
    return true;
  }

  /* 标准 C 纯数学函数 (libm / msvcrt) */
  if (strcmp(fn_name, "sin") == 0 || strcmp(fn_name, "cos") == 0 ||
      strcmp(fn_name, "tan") == 0 || strcmp(fn_name, "asin") == 0 ||
      strcmp(fn_name, "acos") == 0 || strcmp(fn_name, "atan") == 0 ||
      strcmp(fn_name, "atan2") == 0 || strcmp(fn_name, "exp") == 0 ||
      strcmp(fn_name, "log") == 0 || strcmp(fn_name, "log10") == 0 ||
      strcmp(fn_name, "log2") == 0 || strcmp(fn_name, "pow") == 0 ||
      strcmp(fn_name, "sqrt") == 0 || strcmp(fn_name, "ceil") == 0 ||
      strcmp(fn_name, "floor") == 0 || strcmp(fn_name, "fabs") == 0 ||
      strcmp(fn_name, "round") == 0 || strcmp(fn_name, "fmod") == 0) {
    return true;
  }

  /* 标准 C 内存与字符串只读查询 */
  if (strcmp(fn_name, "strlen") == 0 || strcmp(fn_name, "memcmp") == 0 ||
      strcmp(fn_name, "strcmp") == 0 || strcmp(fn_name, "strncmp") == 0) {
    return true;
  }

  return false;
}

static bool is_safe_arc_intermediate(LLVMValueRef inst,
                                     LLVMValueRef target_obj) {
  if (!inst)
    return false;
  LLVMOpcode opcode = LLVMGetInstructionOpcode(inst);

  if (LLVMIsATerminatorInst(inst))
    return false;

  if (opcode == LLVMCall) {
    zan_arc_call_kind_t k = get_arc_call_kind(inst);
    if (k != ARC_NONE) {
      LLVMValueRef op = get_arc_operand(inst);
      if (are_same_arc_object(op, target_obj))
        return false;
      return true;
    }
    LLVMValueRef callee = LLVMGetCalledValue(inst);
    if (callee) {
      const char *fn_name = LLVMGetValueName(callee);
      if (fn_name) {
        if (is_safe_pure_function(fn_name)) {
          return true;
        }
        /* 内存拷贝内联：若目标地址不是 target_obj 且不是 target_obj
         * 的来源槽位，则安全 */
        if (strncmp(fn_name, "llvm.memcpy.", 12) == 0 ||
            strncmp(fn_name, "llvm.memmove.", 13) == 0 ||
            strncmp(fn_name, "llvm.memset.", 12) == 0) {
          if (LLVMGetNumOperands(inst) >= 1) {
            LLVMValueRef dst = strip_pointer_casts(LLVMGetOperand(inst, 0));
            if (dst == target_obj)
              return false;
            if (LLVMIsAInstruction(target_obj) &&
                LLVMGetInstructionOpcode(target_obj) == LLVMLoad) {
              LLVMValueRef src_ptr =
                  strip_pointer_casts(LLVMGetOperand(target_obj, 0));
              if (dst == src_ptr)
                return false;
            }
          }
          return true;
        }
      }
    }
    return false;
  }

  if (opcode == LLVMStore) {
    LLVMValueRef val = LLVMGetOperand(inst, 0);
    LLVMValueRef dst = strip_pointer_casts(LLVMGetOperand(inst, 1));
    if (strip_pointer_casts(val) == target_obj) {
      return false;
    }
    if (LLVMIsAInstruction(target_obj) &&
        LLVMGetInstructionOpcode(target_obj) == LLVMLoad) {
      LLVMValueRef src_ptr = strip_pointer_casts(LLVMGetOperand(target_obj, 0));
      if (dst == src_ptr) {
        return false;
      }
    }
    return true;
  }

  if (opcode == LLVMFence || opcode == LLVMAtomicRMW ||
      opcode == LLVMAtomicCmpXchg) {
    return false;
  }

  return true;
}

static bool is_branch_terminator(LLVMValueRef term) {
  if (!term)
    return false;
  LLVMOpcode op = LLVMGetInstructionOpcode(term);
#if ZAN_LLVM_MAJOR >= 23
  return op == LLVMBr || op == LLVMCondBr;
#else
  return op == LLVMBr;
#endif
}

static int find_bb_in_list(LLVMBasicBlockRef *bbs, int count,
                           LLVMBasicBlockRef bb) {
  for (int i = 0; i < count; i++) {
    if (bbs[i] == bb)
      return i;
  }
  return -1;
}

static bool all_preds_in_dom_set(LLVMBasicBlockRef bb, LLVMBasicBlockRef *bbs,
                                 int count) {
  LLVMValueRef bb_val = LLVMBasicBlockAsValue(bb);
  LLVMUseRef u = LLVMGetFirstUse(bb_val);
  while (u) {
    LLVMValueRef user = LLVMGetUser(u);
    if (user && LLVMIsATerminatorInst(user)) {
      LLVMBasicBlockRef pred = LLVMGetInstructionParent(user);
      if (find_bb_in_list(bbs, count, pred) < 0)
        return false;
    }
    u = LLVMGetNextUse(u);
  }
  return true;
}

static bool try_cross_block_arc(LLVMBasicBlockRef b0, LLVMValueRef retain_inst,
                                zan_arc_call_kind_t k1, LLVMValueRef op1,
                                int start_depth, zan_arc_opt_stats_t *stats) {
  enum { MAX_XBLK_BLOCKS = 24, MAX_XBLK_MATCHES = 32, MAX_XBLK_INSTS = 256 };
  LLVMValueRef term0 = LLVMGetBasicBlockTerminator(b0);
  if (!is_branch_terminator(term0))
    return false;
  LLVMValueRef fn = LLVMGetBasicBlockParent(b0);
  if (!fn)
    return false;
  LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(fn);

  LLVMBasicBlockRef dom_bbs[MAX_XBLK_BLOCKS];
  dom_bbs[0] = b0;
  int dom_count = 1;
  bool grew = true;
  while (grew && dom_count < MAX_XBLK_BLOCKS) {
    grew = false;
    for (int i = 0; i < dom_count && dom_count < MAX_XBLK_BLOCKS; i++) {
      LLVMValueRef term = LLVMGetBasicBlockTerminator(dom_bbs[i]);
      if (!is_branch_terminator(term))
        continue;
      unsigned ns = LLVMGetNumSuccessors(term);
      for (unsigned s = 0; s < ns && dom_count < MAX_XBLK_BLOCKS; s++) {
        LLVMBasicBlockRef succ = LLVMGetSuccessor(term, s);
        if (!succ || succ == entry_bb)
          continue;
        if (find_bb_in_list(dom_bbs, dom_count, succ) >= 0)
          continue;
        if (all_preds_in_dom_set(succ, dom_bbs, dom_count)) {
          dom_bbs[dom_count++] = succ;
          grew = true;
        }
      }
    }
  }
  if (dom_count <= 1)
    return false;

  int entry_bal[MAX_XBLK_BLOCKS];
  for (int i = 0; i < MAX_XBLK_BLOCKS; i++)
    entry_bal[i] = -1;

  int q_bb[MAX_XBLK_BLOCKS * 2];
  int q_bal[MAX_XBLK_BLOCKS * 2];
  int q_head = 0, q_tail = 0;

  unsigned ns0 = LLVMGetNumSuccessors(term0);
  if (ns0 == 0)
    return false;
  for (unsigned s = 0; s < ns0; s++) {
    int si = find_bb_in_list(dom_bbs, dom_count, LLVMGetSuccessor(term0, s));
    if (si <= 0)
      return false;
    if (q_tail >= MAX_XBLK_BLOCKS * 2)
      return false;
    q_bb[q_tail] = si;
    q_bal[q_tail] = start_depth;
    q_tail++;
  }

  LLVMValueRef matches[MAX_XBLK_MATCHES];
  int n_matches = 0;
  int total_insts = 0;

  while (q_head < q_tail) {
    int bi = q_bb[q_head];
    int bal = q_bal[q_head];
    q_head++;

    if (entry_bal[bi] != -1) {
      if (entry_bal[bi] != bal)
        return false;
      continue;
    }
    entry_bal[bi] = bal;

    bool path_balanced = false;
    LLVMValueRef c = LLVMGetFirstInstruction(dom_bbs[bi]);
    while (c) {
      if (++total_insts > MAX_XBLK_INSTS)
        return false;
      if (LLVMIsAPHINode(c)) {
        c = LLVMGetNextInstruction(c);
        continue;
      }
      zan_arc_call_kind_t k2 = get_arc_call_kind(c);
      if (k2 != ARC_NONE) {
        LLVMValueRef op2 = get_arc_operand(c);
        if (are_same_arc_object(op1, op2)) {
          if ((k1 == ARC_RETAIN_OBJ && k2 == ARC_RETAIN_OBJ) ||
              (k1 == ARC_RETAIN_STR && k2 == ARC_RETAIN_STR)) {
            bal++;
          } else if ((k1 == ARC_RETAIN_OBJ && k2 == ARC_RELEASE_OBJ) ||
                     (k1 == ARC_RETAIN_STR && k2 == ARC_RELEASE_STR)) {
            bal--;
            if (bal == 0) {
              if (n_matches >= MAX_XBLK_MATCHES)
                return false;
              matches[n_matches++] = c;
              path_balanced = true;
              break;
            }
          }
        } else if (k2 == ARC_RELEASE_OBJ || k2 == ARC_RELEASE_STR) {
          return false;
        }
      }

      if (LLVMIsATerminatorInst(c))
        break;
      if (!is_safe_arc_intermediate(c, op1))
        return false;
      c = LLVMGetNextInstruction(c);
    }

    if (!path_balanced) {
      LLVMValueRef term = LLVMGetBasicBlockTerminator(dom_bbs[bi]);
      if (!is_branch_terminator(term))
        return false;
      unsigned ns = LLVMGetNumSuccessors(term);
      if (ns == 0)
        return false;
      for (unsigned s = 0; s < ns; s++) {
        int si = find_bb_in_list(dom_bbs, dom_count, LLVMGetSuccessor(term, s));
        if (si <= 0 || q_tail >= MAX_XBLK_BLOCKS * 2)
          return false;
        q_bb[q_tail] = si;
        q_bal[q_tail] = bal;
        q_tail++;
      }
    }
  }

  if (n_matches == 0)
    return false;
  for (int i = 0; i < n_matches; i++)
    LLVMInstructionEraseFromParent(matches[i]);
  LLVMInstructionEraseFromParent(retain_inst);
  stats->pairs_elided++;
  return true;
}

static zan_arc_opt_stats_t zan_opt_arc_mod(LLVMModuleRef mod,
                                           zan_opt_level_t level) {
  zan_arc_opt_stats_t stats = {0, 0, 0};
  if (!mod || level == ZAN_OPT_NONE)
    return stats;

  LLVMValueRef fn = LLVMGetFirstFunction(mod);

  while (fn) {
    if (LLVMIsDeclaration(fn)) {
      fn = LLVMGetNextFunction(fn);
      continue;
    }
    bool fn_changed = true;
    int fn_pass = 0;
    while (fn_changed && fn_pass < 8) {
      fn_changed = false;
      fn_pass++;
      LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
      while (bb) {
        LLVMValueRef inst = LLVMGetFirstInstruction(bb);
        while (inst) {
          LLVMValueRef next = LLVMGetNextInstruction(inst);
          zan_arc_call_kind_t k1 = get_arc_call_kind(inst);

          if (k1 != ARC_NONE) {
            LLVMValueRef op = get_arc_operand(inst);
            if (!op || is_immortal_arc_value(mod, op, NULL, 0, 0)) {
              LLVMInstructionEraseFromParent(inst);
              stats.pairs_elided++;
              fn_changed = true;
              inst = next;
              continue;
            }
          }

          if (k1 == ARC_RETAIN_OBJ || k1 == ARC_RETAIN_STR) {
            LLVMValueRef op1 = get_arc_operand(inst);
            if (op1) {
              LLVMValueRef curr = next;
              int steps = 0;
              const int MAX_ARC_WINDOW = 128;
              LLVMValueRef match_release = NULL;
              int retain_depth = 1;
              bool hit_block_end = false;

              while (curr && steps < MAX_ARC_WINDOW) {
                if (LLVMIsATerminatorInst(curr)) {
                  hit_block_end = true;
                  break;
                }
                zan_arc_call_kind_t k2 = get_arc_call_kind(curr);
                if (k2 != ARC_NONE) {
                  LLVMValueRef op2 = get_arc_operand(curr);
                  if (are_same_arc_object(op1, op2)) {
                    if ((k1 == ARC_RETAIN_OBJ && k2 == ARC_RETAIN_OBJ) ||
                        (k1 == ARC_RETAIN_STR && k2 == ARC_RETAIN_STR)) {
                      retain_depth++;
                    } else if ((k1 == ARC_RETAIN_OBJ &&
                                k2 == ARC_RELEASE_OBJ) ||
                               (k1 == ARC_RETAIN_STR &&
                                k2 == ARC_RELEASE_STR)) {
                      retain_depth--;
                      if (retain_depth == 0) {
                        match_release = curr;
                        break;
                      }
                    }
                  } else {
                    /* B-ID96：异对象 ARC 释放不安全——别名判定只认
                     * SSA/同槽 load、不追 GEP 与容器级联，异对象
                     * release 可能在运行期经容器元素/字段别名动到
                     * 目标对象的引用计数。保守关窗；
                     * 异对象 retain 不改目标计数，仍可穿窗。 */
                    if (k2 == ARC_RELEASE_OBJ || k2 == ARC_RELEASE_STR) {
                      break;
                    }
                  }
                }

                if (!is_safe_arc_intermediate(curr, op1)) {
                  break;
                }
                curr = LLVMGetNextInstruction(curr);
                steps++;
              }
              if (!curr)
                hit_block_end = true;

              if (match_release) {
                LLVMValueRef after_retain = LLVMGetNextInstruction(inst);
                if (after_retain == match_release) {
                  after_retain = LLVMGetNextInstruction(match_release);
                }
                LLVMInstructionEraseFromParent(match_release);
                LLVMInstructionEraseFromParent(inst);
                stats.pairs_elided++;
                fn_changed = true;
                inst = after_retain;
                continue;
              }

              if (hit_block_end && retain_depth > 0 &&
                  try_cross_block_arc(bb, inst, k1, op1, retain_depth,
                                      &stats)) {
                fn_changed = true;
                inst = next;
                continue;
              }
            }
          }

          inst = next;
        }
        bb = LLVMGetNextBasicBlock(bb);
      }
    }
    fn = LLVMGetNextFunction(fn);
  }

  return stats;
}

zan_arc_opt_stats_t zan_opt_arc(zan_irgen_t *g, zan_opt_level_t level) {
  if (!g || !g->mod)
    return (zan_arc_opt_stats_t){0, 0, 0};
  return zan_opt_arc_mod(g->mod, level);
}

static LLVMValueRef unwrap_const_bitcasts(LLVMValueRef v) {
  int d = 0;
  while (v && LLVMIsAConstantExpr(v) &&
         LLVMGetConstOpcode(v) == LLVMBitCast && d < 8) {
    v = LLVMGetOperand(v, 0);
    d++;
  }
  return v;
}

static LLVMValueRef lookup_vtable_slot_fn(LLVMValueRef vt_gv,
                                          unsigned long long slot) {
  if (!vt_gv || !LLVMIsAGlobalVariable(vt_gv) || !LLVMIsGlobalConstant(vt_gv))
    return NULL;
  LLVMValueRef init = LLVMGetInitializer(vt_gv);
  if (!init)
    return NULL;
  const char *vt_name = LLVMGetValueName(vt_gv);
  if (!vt_name || strstr(vt_name, "_vtable") == NULL)
    return NULL;
  LLVMValueRef agg = unwrap_const_bitcasts(init);
  unsigned agg_ops = agg ? (unsigned)LLVMGetNumOperands(agg) : 0;
  if (!agg || slot >= agg_ops)
    return NULL;
  LLVMValueRef elem = unwrap_const_bitcasts(LLVMGetOperand(agg, (unsigned)slot));
  return (elem && LLVMIsAFunction(elem)) ? elem : NULL;
}

static LLVMValueRef resolve_vtable_ptr_slot(LLVMModuleRef mod, LLVMValueRef ptr,
                                            unsigned long long *out_slot,
                                            int depth) {
  if (!ptr || depth > 4)
    return NULL;
  ptr = strip_pointer_casts(ptr);
  if (!ptr)
    return NULL;

  /* instcombine/GVN may fold GEP(@__zan_vtable_X, 0, 0) to @__zan_vtable_X */
  if (LLVMIsAGlobalVariable(ptr)) {
    const char *nm = LLVMGetValueName(ptr);
    if (nm && strstr(nm, "_vtable")) {
      LLVMValueRef fn = lookup_vtable_slot_fn(ptr, 0);
      if (fn && out_slot)
        *out_slot = 0;
      return fn;
    }
    return NULL;
  }

  LLVMValueRef base = NULL;
  uint64_t byte_off = 0;
  if (gep_const_byte_offset(mod, ptr, &base, &byte_off)) {
    if (base && LLVMIsAGlobalVariable(base)) {
      const char *nm = LLVMGetValueName(base);
      if (nm && strstr(nm, "_vtable")) {
        LLVMTargetDataRef td = LLVMGetModuleDataLayout(mod);
        LLVMTypeRef i8ptr = LLVMPointerType(
            LLVMInt8TypeInContext(LLVMGetModuleContext(mod)), 0);
        uint64_t ptr_sz = td ? LLVMABISizeOfType(td, i8ptr) : 8;
        if (ptr_sz > 0 && (byte_off % ptr_sz) == 0) {
          unsigned long long slot = byte_off / ptr_sz;
          LLVMValueRef fn = lookup_vtable_slot_fn(base, slot);
          if (fn && out_slot)
            *out_slot = slot;
          return fn;
        }
      }
    }
    return NULL;
  }

  if (LLVMIsAInstruction(ptr)) {
    LLVMOpcode op = LLVMGetInstructionOpcode(ptr);
    if (op == LLVMSelect && LLVMGetNumOperands(ptr) >= 3) {
      unsigned long long s1 = 0, s2 = 0;
      LLVMValueRef f1 =
          resolve_vtable_ptr_slot(mod, LLVMGetOperand(ptr, 1), &s1, depth + 1);
      LLVMValueRef f2 =
          resolve_vtable_ptr_slot(mod, LLVMGetOperand(ptr, 2), &s2, depth + 1);
      if (f1 && f1 == f2) {
        if (out_slot)
          *out_slot = s1;
        return f1;
      }
    } else if (op == LLVMPHI) {
      unsigned n = LLVMCountIncoming(ptr);
      if (n > 0 && n <= 8) {
        LLVMValueRef common = NULL;
        unsigned long long common_s = 0;
        for (unsigned i = 0; i < n; i++) {
          unsigned long long si = 0;
          LLVMValueRef fi = resolve_vtable_ptr_slot(
              mod, LLVMGetIncomingValue(ptr, i), &si, depth + 1);
          if (!fi)
            return NULL;
          if (!common) {
            common = fi;
            common_s = si;
          } else if (fi != common) {
            return NULL;
          }
        }
        if (common && out_slot)
          *out_slot = common_s;
        return common;
      }
    }
  }
  return NULL;
}

static bool vtable_is_possibly_instantiated(LLVMValueRef value, int depth) {
  if (!value)
    return false;
  if (depth > 16)
    return true;
  for (LLVMUseRef u = LLVMGetFirstUse(value); u; u = LLVMGetNextUse(u)) {
    LLVMValueRef user = LLVMGetUser(u);
    if (!user)
      return true;
    if (LLVMIsAInstruction(user)) {
      LLVMOpcode op = LLVMGetInstructionOpcode(user);
      if (op == LLVMICmp)
        continue;
      if (op == LLVMBitCast || op == LLVMAddrSpaceCast) {
        if (vtable_is_possibly_instantiated(user, depth + 1))
          return true;
        continue;
      }
      return true;
    }
    if (LLVMIsAConstantExpr(user)) {
      LLVMOpcode cop = LLVMGetConstOpcode(user);
      if (cop == LLVMBitCast || cop == LLVMAddrSpaceCast) {
        if (vtable_is_possibly_instantiated(user, depth + 1))
          return true;
        continue;
      }
      return true;
    }
    return true;
  }
  return false;
}

static void replace_callee_operand(LLVMBuilderRef b, LLVMValueRef inst,
                                   LLVMValueRef orig_callee,
                                   LLVMValueRef fn_elem) {
  LLVMValueRef target = fn_elem;
  if (LLVMTypeOf(target) != LLVMTypeOf(orig_callee)) {
    LLVMPositionBuilderBefore(b, inst);
    target = LLVMBuildBitCast(b, fn_elem, LLVMTypeOf(orig_callee), "devirt.fn");
  }
  unsigned num_ops = (unsigned)LLVMGetNumOperands(inst);
  for (unsigned k = 0; k < num_ops; k++) {
    if (LLVMGetOperand(inst, k) == orig_callee) {
      LLVMSetOperand(inst, k, target);
      break;
    }
  }
}

static bool struct_idx_is_subclass_of(const zan_irgen_t *g, int sub_idx,
                                      int base_idx) {
  if (!g || sub_idx < 0 || base_idx < 0 || sub_idx >= g->struct_type_count ||
      base_idx >= g->struct_type_count)
    return false;
  int cur = sub_idx;
  for (int steps = 0; cur >= 0 && cur < g->struct_type_count &&
                      steps <= g->struct_type_count;
       steps++) {
    if (cur == base_idx)
      return true;
    cur = g->struct_types[cur].base_struct_idx;
  }
  return false;
}

static LLVMValueRef find_single_live_vcall_impl(zan_irgen_t *g,
                                                LLVMModuleRef mod,
                                                int st_idx,
                                                unsigned long long slot) {
  if (!g || st_idx < 0 || st_idx >= g->struct_type_count)
    return NULL;
  LLVMValueRef single_fn = NULL;
  for (int i = 0; i < g->struct_type_count; i++) {
    if (!struct_idx_is_subclass_of(g, i, st_idx))
      continue;
    if (g->struct_types[i].devirt_abstract)
      continue;
    if (g->struct_types[i].devirt_generic)
      return NULL;
    const char *vt_name = g->struct_types[i].vtable_name;
    if (!vt_name)
      continue;
    LLVMValueRef gv = LLVMGetNamedGlobal(mod, vt_name);
    if (!gv)
      continue;
    if (!vtable_is_possibly_instantiated(gv, 0))
      continue;
    LLVMValueRef fn = lookup_vtable_slot_fn(gv, slot);
    if (!fn)
      return NULL;
    if (!single_fn)
      single_fn = fn;
    else if (single_fn != fn)
      return NULL;
  }
  return single_fn;
}

static zan_devirt_stats_t zan_opt_devirt_mod(LLVMModuleRef mod, zan_irgen_t *g) {
  zan_devirt_stats_t stats = {0, 0};
  if (!mod)
    return stats;

  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  LLVMBuilderRef b = LLVMCreateBuilderInContext(ctx);
  bool closed_world =
      (g != NULL && !g->emit_lib && g->publish_instance_prune &&
       !g->sharded_module);

  if (closed_world) {
    LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx);
    LLVMValueRef vtrue = LLVMConstInt(i1, 1, 0);
    LLVMValueRef vfalse = LLVMConstInt(i1, 0, 0);
    for (LLVMValueRef gv = LLVMGetFirstGlobal(mod); gv;
         gv = LLVMGetNextGlobal(gv)) {
      const char *nm = LLVMGetValueName(gv);
      if (!nm || strncmp(nm, "__zan_vtable_", 13) != 0)
        continue;
      if (!LLVMGetInitializer(gv) || LLVMIsDeclaration(gv))
        continue;
      if (vtable_is_possibly_instantiated(gv, 0))
        continue;
      /* Collect all ICmp users of gv (through bitcasts) and fold them. */
      LLVMValueRef work[64];
      int nw = 0;
      work[nw++] = gv;
      for (int wi = 0; wi < nw; wi++) {
        for (LLVMUseRef u = LLVMGetFirstUse(work[wi]); u;) {
          LLVMUseRef next_u = LLVMGetNextUse(u);
          LLVMValueRef user = LLVMGetUser(u);
          bool erased = false;
          if (user && LLVMIsAICmpInst(user)) {
            LLVMIntPredicate pred = LLVMGetICmpPredicate(user);
            if (pred == LLVMIntEQ) {
              LLVMReplaceAllUsesWith(user, vfalse);
              LLVMInstructionEraseFromParent(user);
              stats.interfaces_resolved++;
              erased = true;
            } else if (pred == LLVMIntNE) {
              LLVMReplaceAllUsesWith(user, vtrue);
              LLVMInstructionEraseFromParent(user);
              stats.interfaces_resolved++;
              erased = true;
            }
          } else if (user &&
                     ((LLVMIsAInstruction(user) &&
                       (LLVMGetInstructionOpcode(user) == LLVMBitCast ||
                        LLVMGetInstructionOpcode(user) == LLVMAddrSpaceCast)) ||
                      (LLVMIsAConstantExpr(user) &&
                       (LLVMGetConstOpcode(user) == LLVMBitCast ||
                        LLVMGetConstOpcode(user) == LLVMAddrSpaceCast)))) {
            if (nw < 64)
              work[nw++] = user;
          }
          u = erased ? LLVMGetFirstUse(work[wi]) : next_u;
        }
      }
    }
  }

  unsigned vcall_md = LLVMGetMDKindIDInContext(ctx, "zan.vcall", 9);
  LLVMValueRef fn = LLVMGetFirstFunction(mod);

  while (fn) {
    if (LLVMIsDeclaration(fn)) {
      fn = LLVMGetNextFunction(fn);
      continue;
    }
    LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
    while (bb) {
      LLVMValueRef inst = LLVMGetFirstInstruction(bb);
      while (inst) {
        LLVMValueRef next = LLVMGetNextInstruction(inst);
        LLVMOpcode op = LLVMGetInstructionOpcode(inst);
        if (op == LLVMCall || op == LLVMInvoke) {
          LLVMValueRef callee = LLVMGetCalledValue(inst);
          if (callee && !LLVMIsAFunction(callee)) {
            LLVMValueRef resolved = NULL;
            LLVMValueRef cstripped = strip_pointer_casts(callee);
            if (cstripped && LLVMIsAInstruction(cstripped) &&
                LLVMGetInstructionOpcode(cstripped) == LLVMLoad) {
              LLVMValueRef ptr = LLVMGetOperand(cstripped, 0);
              resolved = resolve_vtable_ptr_slot(mod, ptr, NULL, 0);
            }
            if (!resolved && closed_world) {
              LLVMValueRef md = LLVMGetMetadata(inst, vcall_md);
              if (md && LLVMGetNumOperands(md) == 2) {
                LLVMValueRef v0 = LLVMGetOperand(md, 0);
                LLVMValueRef v1 = LLVMGetOperand(md, 1);
                if (v0 && LLVMIsAConstantInt(v0) && v1 &&
                    LLVMIsAConstantInt(v1)) {
                  int st_idx = (int)LLVMConstIntGetSExtValue(v0);
                  unsigned long long slot = LLVMConstIntGetZExtValue(v1);
                  resolved = find_single_live_vcall_impl(g, mod, st_idx, slot);
                }
              }
            }
            if (resolved) {
              replace_callee_operand(b, inst, callee, resolved);
              if (LLVMGetMetadata(inst, vcall_md))
                LLVMSetMetadata(inst, vcall_md, NULL);
              stats.calls_devirtualized++;
            }
          }
        }
        inst = next;
      }
      bb = LLVMGetNextBasicBlock(bb);
    }
    fn = LLVMGetNextFunction(fn);
  }

  LLVMDisposeBuilder(b);
  return stats;
}

zan_devirt_stats_t zan_opt_devirtualize(zan_irgen_t *g, zan_binder_t *binder) {
  (void)binder;
  if (!g || !g->mod)
    return (zan_devirt_stats_t){0, 0};
  return zan_opt_devirt_mod(g->mod, g);
}

static zan_constfold_stats_t zan_opt_const_fold_mod(LLVMModuleRef mod) {
  zan_constfold_stats_t stats = {0, 0, 0};
  if (!mod)
    return stats;

  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  LLVMBuilderRef b_shared = LLVMCreateBuilderInContext(ctx);
  LLVMValueRef fn = LLVMGetFirstFunction(mod);

  while (fn) {
    if (LLVMIsDeclaration(fn)) {
      fn = LLVMGetNextFunction(fn);
      continue;
    }
    bool fn_changed = true;
    int pass = 0;
    while (fn_changed && pass < 4) {
      fn_changed = false;
      pass++;
      LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
      while (bb) {
        LLVMValueRef inst = LLVMGetFirstInstruction(bb);
        while (inst) {
          LLVMValueRef next = LLVMGetNextInstruction(inst);
          unsigned opcode = LLVMGetInstructionOpcode(inst);

          if (opcode == LLVMAdd || opcode == LLVMSub || opcode == LLVMMul ||
              opcode == LLVMSDiv) {
            LLVMValueRef lhs = LLVMGetOperand(inst, 0);
            LLVMValueRef rhs = LLVMGetOperand(inst, 1);
            if (LLVMIsAConstantInt(lhs) && LLVMIsAConstantInt(rhs)) {
              long long v1 = (long long)LLVMConstIntGetSExtValue(lhs);
              long long v2 = (long long)LLVMConstIntGetSExtValue(rhs);
              bool can_fold = true;
              long long res = 0;
              if (opcode == LLVMAdd)
                res = v1 + v2;
              else if (opcode == LLVMSub)
                res = v1 - v2;
              else if (opcode == LLVMMul)
                res = v1 * v2;
              else if (opcode == LLVMSDiv) {
                if (v2 != 0)
                  res = v1 / v2;
                else
                  can_fold = false;
              }
              if (can_fold) {
                LLVMTypeRef ty = LLVMTypeOf(inst);
                LLVMValueRef folded =
                    LLVMConstInt(ty, (unsigned long long)res, 1);
                LLVMReplaceAllUsesWith(inst, folded);
                LLVMInstructionEraseFromParent(inst);
                stats.constants_folded++;
                fn_changed = true;
                inst = next;
                continue;
              }
            }
          }

#if ZAN_LLVM_MAJOR >= 23
          if (opcode == LLVMCondBr && LLVMGetNumOperands(inst) == 3) {
#else
          if (opcode == LLVMBr && LLVMGetNumOperands(inst) == 3) {
#endif
            LLVMValueRef cond = LLVMGetCondition(inst);
            if (cond && LLVMIsAConstantInt(cond)) {
              unsigned long long cval = LLVMConstIntGetZExtValue(cond);
              LLVMBasicBlockRef target_bb = (cval != 0)
                                                ? LLVMGetSuccessor(inst, 0)
                                                : LLVMGetSuccessor(inst, 1);
              LLVMBasicBlockRef dead_bb = (cval != 0)
                                              ? LLVMGetSuccessor(inst, 1)
                                              : LLVMGetSuccessor(inst, 0);
              if (target_bb) {
                /* 折叠分支删掉"本块→dead_bb"这条边时，dead_bb 的
                 * PHI 里来自本块的 incoming 条目必须一并删除——
                 * 否则模块非法（PHINode 缺 predecessor 条目）。
                 * A595：非法 IR 曾让 VerifyModule 静默失败、
                 * default<O1> 整条管线被跳过，后续 pass 和后端
                 * 在非法 IR 上产出错码（字符串长度探测误报 0）。 */
                if (dead_bb && dead_bb != target_bb) {
                  LLVMValueRef phi = LLVMGetFirstInstruction(dead_bb);
                  while (phi && LLVMIsAPHINode(phi)) {
                    LLVMValueRef phi_next = LLVMGetNextInstruction(phi);
                    unsigned n = LLVMCountIncoming(phi);
                    bool has_this = false;
                    for (unsigned i = 0; i < n; i++) {
                      if (LLVMGetIncomingBlock(phi, i) == bb) {
                        has_this = true;
                        break;
                      }
                    }
                    if (has_this) {
                      LLVMValueRef *vals = (LLVMValueRef *)malloc(
                          sizeof(LLVMValueRef) * (n > 0 ? n : 1));
                      LLVMBasicBlockRef *blks = (LLVMBasicBlockRef *)malloc(
                          sizeof(LLVMBasicBlockRef) * (n > 0 ? n : 1));
                      unsigned m = 0;
                      if (vals && blks) {
                        for (unsigned i = 0; i < n; i++) {
                          if (LLVMGetIncomingBlock(phi, i) == bb)
                            continue;
                          vals[m] = LLVMGetIncomingValue(phi, i);
                          blks[m] = LLVMGetIncomingBlock(phi, i);
                          m++;
                        }
                      }
                      LLVMPositionBuilderBefore(b_shared, phi);
                      if (m > 0) {
                        LLVMValueRef np =
                            LLVMBuildPhi(b_shared, LLVMTypeOf(phi), "");
                        LLVMAddIncoming(np, vals, blks, m);
                        LLVMReplaceAllUsesWith(phi, np);
                      } else {
                        /* 本块是唯一前驱：边删后 dead_bb 不可达，
                         * PHI 悬空——用 undef 顶替引用，块本身
                         * 交给后续死代码清理。 */
                        LLVMReplaceAllUsesWith(phi,
                                               LLVMConstNull(LLVMTypeOf(phi)));
                      }
                      LLVMInstructionEraseFromParent(phi);
                      free(vals);
                      free(blks);
                    }
                    phi = phi_next;
                  }
                }
                LLVMPositionBuilderBefore(b_shared, inst);
                LLVMBuildBr(b_shared, target_bb);
                LLVMInstructionEraseFromParent(inst);
                stats.branches_eliminated++;
                fn_changed = true;
                inst = next;
                continue;
              }
            }
          }

          inst = next;
        }
        bb = LLVMGetNextBasicBlock(bb);
      }
    }
    fn = LLVMGetNextFunction(fn);
  }

  LLVMDisposeBuilder(b_shared);
  return stats;
}

zan_constfold_stats_t zan_opt_const_fold(zan_irgen_t *g) {
  if (!g || !g->mod)
    return (zan_constfold_stats_t){0, 0, 0};
  return zan_opt_const_fold_mod(g->mod);
}

static zan_dce_stats_t zan_opt_dce_mod(LLVMModuleRef mod) {
  zan_dce_stats_t stats = {0, 0, 0};
  if (!mod)
    return stats;

  LLVMValueRef fn = LLVMGetFirstFunction(mod);

  while (fn) {
    if (LLVMIsDeclaration(fn)) {
      fn = LLVMGetNextFunction(fn);
      continue;
    }
    bool changed = true;
    int pass = 0;
    while (changed && pass < 4) {
      changed = false;
      pass++;
      LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
      while (bb) {
        LLVMValueRef inst = LLVMGetFirstInstruction(bb);
        while (inst) {
          LLVMValueRef next = LLVMGetNextInstruction(inst);
          unsigned opcode = LLVMGetInstructionOpcode(inst);

          if (opcode == LLVMAlloca || opcode == LLVMAdd || opcode == LLVMSub ||
              opcode == LLVMMul || opcode == LLVMBitCast ||
              opcode == LLVMAddrSpaceCast || opcode == LLVMZExt ||
              opcode == LLVMSExt || opcode == LLVMTrunc ||
              opcode == LLVMICmp) {
            if (!LLVMGetFirstUse(inst)) {
              LLVMInstructionEraseFromParent(inst);
              stats.dead_stores++;
              changed = true;
              inst = next;
              continue;
            }
          }
          inst = next;
        }
        bb = LLVMGetNextBasicBlock(bb);
      }
    }
    fn = LLVMGetNextFunction(fn);
  }

  return stats;
}

zan_dce_stats_t zan_opt_dce(zan_irgen_t *g) {
  if (!g || !g->mod)
    return (zan_dce_stats_t){0, 0, 0};
  return zan_opt_dce_mod(g->mod);
}

zan_inline_stats_t zan_opt_inline(zan_irgen_t *g, zan_opt_level_t level) {
  (void)g;
  (void)level;
  /* ARC 跨内联边界生命周期受控，交由 LLVM 标准成本模型决策 */
  zan_inline_stats_t stats = {0, 0};
  return stats;
}

#if ZAN_LLVM_MAJOR >= 23
/* LLVM 23 删掉了 default<Os>/<Oz> 管线：体积优化改为 O2 管线 + 函数级
 * optsize（Oz 再加 minsize）。按模块打标，分片发布（shard_emit.c）的每个
 * 分片模块也经 zan_opt_run_passes_on_module 走到这里。 */
static void zan_opt_mark_size(LLVMModuleRef mod, bool min_size) {
  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  LLVMAttributeRef opt = LLVMCreateEnumAttribute(
      ctx, LLVMGetEnumAttributeKindForName("optsize", 7), 0);
  LLVMAttributeRef mins =
      min_size ? LLVMCreateEnumAttribute(
                     ctx, LLVMGetEnumAttributeKindForName("minsize", 7), 0)
               : NULL;
  for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn;
       fn = LLVMGetNextFunction(fn)) {
    if (LLVMIsDeclaration(fn))
      continue;
    LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)(-1), opt);
    if (mins)
      LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)(-1), mins);
  }
}
#endif

zan_status_t zan_opt_validate_cpu(zan_irgen_t *g, LLVMModuleRef mod) {
  if (!g->cpu_profile)
    return ZAN_OK;
  /* Also preserve explicit requirements for external consumers of --emit-ir.
   * The TargetMachine remains authoritative for optimization and emission. */
  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn;
       fn = LLVMGetNextFunction(fn)) {
    if (LLVMIsDeclaration(fn))
      continue;
    LLVMAddAttributeAtIndex(
        fn, (LLVMAttributeIndex)-1,
        LLVMCreateStringAttribute(ctx, "target-cpu", 10, g->target_cpu,
                                  (unsigned)strlen(g->target_cpu)));
    LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)-1,
                            LLVMCreateStringAttribute(
                                ctx, "target-features", 15, g->target_features,
                                (unsigned)strlen(g->target_features)));
  }
  if (strcmp(g->cpu_profile, "baseline") != 0)
    return ZAN_OK;
  for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn;
       fn = LLVMGetNextFunction(fn)) {
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
         bb = LLVMGetNextBasicBlock(bb)) {
      for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
           inst = LLVMGetNextInstruction(inst)) {
        if (!LLVMIsACallInst(inst) && !LLVMIsAInvokeInst(inst))
          continue;
        LLVMValueRef callee = LLVMGetCalledValue(inst);
        const char *name = callee ? LLVMGetValueName(callee) : NULL;
        if (!name || strncmp(name, "llvm.x86.", 9) != 0)
          continue;
        /* SSE and SSE2 are guaranteed by x86-64. Anything newer
         * must have taken a portable lowering, or fail before LLVM
         * instruction selection (which may abort on such intrinsics). */
        if (strncmp(name, "llvm.x86.sse.", 13) == 0 ||
            strncmp(name, "llvm.x86.sse2.", 14) == 0)
          continue;
        fprintf(stderr,
                "error: --cpu-profile baseline does not support hardware "
                "intrinsic '%s' used by '%s'; use --cpu-profile avx2\n",
                name, LLVMGetValueName(fn));
        return ZAN_ERROR;
      }
    }
  }
  return ZAN_OK;
}

zan_status_t zan_opt_verify_module(LLVMModuleRef mod, const char *stage) {
  if (!mod) {
    fprintf(stderr, "error: missing LLVM module %s\n", stage);
    return ZAN_ERROR;
  }
  char *msg = NULL;
  bool failed = LLVMVerifyModule(mod, LLVMReturnStatusAction, &msg) != 0;
  if (failed)
    fprintf(stderr, "error: module verification failed %s: %s\n", stage,
            msg ? msg : "(no message)");
  if (msg)
    LLVMDisposeMessage(msg);
  return failed ? ZAN_ERROR : ZAN_OK;
}

/* All LLVM pipelines share the same fail-closed protocol. */
static zan_status_t run_checked_passes(LLVMModuleRef mod,
                                       LLVMTargetMachineRef tm,
                                       const char *passes) {
  if (zan_opt_verify_module(mod, "before pass pipeline") != ZAN_OK)
    return ZAN_ERROR;
  LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
  LLVMPassBuilderOptionsSetVerifyEach(opts, 0);
  LLVMPassBuilderOptionsSetDebugLogging(opts, 0);
  LLVMErrorRef err = LLVMRunPasses(mod, passes, tm, opts);
  LLVMDisposePassBuilderOptions(opts);
  if (err) {
    char *msg = LLVMGetErrorMessage(err);
    fprintf(stderr, "error: LLVM pass pipeline '%s' failed: %s\n", passes, msg);
    LLVMDisposeErrorMessage(msg);
    return ZAN_ERROR;
  }
  return zan_opt_verify_module(mod, "after pass pipeline");
}

/* ===================== 收缩 pass：nothrow 闭包 + invoke 塌缩
 * ===================== 账本 §3.2 全局 pass ②（实施收口见账本 §10）。Zan
 * 在任何带 landing pad 作用域内的调用一律降级为 invoke（irgen_rt.c
 * zan_call_or_invoke， lpad_depth>0 即
 * invoke），对可证不抛的调用是纯体积税：每站点多 norm/lpad 两个后继、landingpad
 * cleanup、__zan_eh_state/tmp_top 簿记。
 *
 * 契约（design.md §11 S-UNWIND）：
 *   nothrow(F) ⇔ F 无直接 raise 源调用（RaiseException/__cxa_throw，
 *   fatal 形态 RaiseException→exit→unreachable 除外——personality 拒
 *   收自定义 SEH code，无 landing pad 可达）
 *             ∧ 所有直接被调者 nothrow。
 *   extern 播种（失败方向 = 少塌缩）：Zan 名注册表命中（跨片 declare，
 *   zan_opt_note_zan_fn_name 喂入）→ MAYTHROW；__zan_eh_tmp_unwind
 *   （重抛）与 raise 源 → MAYTHROW；__cxa_/__gxx_/llvm./zan_/__zan_
 *   前缀（native runtime/驱动/编译器助手，纯 C 无 cxa）与 CRT/Win32
 *   精确名表 → nothrow；其余 extern 一律保守 MAYTHROW。递归由不动点
 *   自然处理（初值 UNKNOWN，无证据不晋级）。
 * 塌缩：仅被调者可解析为 Function 且 nothrow 的 invoke → call + br
 * normal（call conv/call-site 属性逐槽保留；invoke 无 tail 语义，C API
 * LLVMIsTailCall 按 CallInst 强转 InvokeInst，禁用）。personality 语义
 * 不变：可抛调用一个不动，剩余 invoke 的 lpad 原样保留，死 lpad 交
 * LLVM 管线清理。行为等价性由 nothrow 定义保证（不抛者调用序列无
 * unwind 可观察差异）。LLVM C API 三陷阱（Erase 非 Remove / PHI 前驱
 * 重建 / 站点函数类型）实录见 design.md §11.1。
 */
typedef enum {
  SHRINK_UNKNOWN = 0,
  SHRINK_NOTHROW = 1,
  SHRINK_MAYTHROW = 2
} shrink_state_t;

typedef struct {
  LLVMValueRef *keys;
  int *vals;
  unsigned mask;
} shrink_map_t;

static unsigned shrink_hash_ptr(const void *p) {
  unsigned long long v = (unsigned long long)(uintptr_t)p;
  v ^= v >> 17;
  v *= 0x9E3779B9U;
  v ^= v >> 13;
  return (unsigned)v;
}

static bool shrink_map_init(shrink_map_t *m, unsigned cap_hint) {
  unsigned cap = 1024;
  while (cap < cap_hint * 2u)
    cap <<= 1;
  m->keys = (LLVMValueRef *)calloc(cap, sizeof(LLVMValueRef));
  m->vals = (int *)calloc(cap, sizeof(int));
  if (!m->keys || !m->vals)
    return false;
  m->mask = cap - 1;
  return true;
}

/* 键不存在时插入并以 *fresh 标记；返回槽位指针 */
static int *shrink_map_slot(shrink_map_t *m, LLVMValueRef k, bool *fresh) {
  unsigned i = shrink_hash_ptr(k) & m->mask;
  while (m->keys[i] && m->keys[i] != k)
    i = (i + 1) & m->mask;
  if (fresh)
    *fresh = (m->keys[i] == NULL);
  m->keys[i] = k;
  return &m->vals[i];
}

static void shrink_map_free(shrink_map_t *m) {
  free(m->keys);
  free(m->vals);
  m->keys = NULL;
  m->vals = NULL;
  m->mask = 0;
}

/* raise 源（Zan EH 仅由这两个符号及 guard 抛异常助手发起）。 */
static bool shrink_is_raise_source(const char *n) {
  return strcmp(n, "RaiseException") == 0 || strcmp(n, "__cxa_throw") == 0 ||
         strcmp(n, "__zan_throw_guard") == 0;
}

/* ---- Zan 函数名注册表（irgen_register_function 逐个喂入）----
 * publish 跨片时，别的分片的 Zan 函数在本模块只是 declaration——
 * 名字形状与 C 导入无法区分（App_FrameApp vs CancelIoEx），只能靠
 * 编译器自己的发射记录。registry 命中 ⇒ 该 declaration 是 Zan 函数
 * ⇒ 可抛。内容哈希 + strcmp：LLVM 函数名指针随分片模块销毁重建会
 * 变，不能按指针判等；strdup 自持串，生命周期跨模块。 */
static unsigned shrink_hash_str(const char *s) {
  unsigned long long v = 1469598103934665603ULL;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    v ^= *p;
    v *= 1099511628211ULL;
  }
  return (unsigned)v;
}

typedef struct {
  char **keys;
  unsigned mask;
} shrink_strset_t;

static shrink_strset_t shrink_zan_set;
static bool shrink_zan_set_ready;

static bool shrink_strset_init(shrink_strset_t *m, unsigned cap_hint) {
  unsigned cap = 1024;
  while (cap < cap_hint * 2u)
    cap <<= 1;
  m->keys = (char **)calloc(cap, sizeof(char *));
  if (!m->keys)
    return false;
  m->mask = cap - 1;
  return true;
}

static void shrink_strset_add(shrink_strset_t *m, const char *s) {
  unsigned i = shrink_hash_str(s) & m->mask;
  while (m->keys[i]) {
    if (strcmp(m->keys[i], s) == 0)
      return;
    i = (i + 1) & m->mask;
  }
  m->keys[i] = strdup(s);
}

static bool shrink_strset_has(shrink_strset_t *m, const char *s) {
  if (!shrink_zan_set_ready)
    return false;
  unsigned i = shrink_hash_str(s) & m->mask;
  while (m->keys[i]) {
    if (strcmp(m->keys[i], s) == 0)
      return true;
    i = (i + 1) & m->mask;
  }
  return false;
}

void zan_opt_note_zan_fn_name(const char *name) {
  if (!name || !*name)
    return;
  if (!shrink_zan_set_ready) {
    if (!shrink_strset_init(&shrink_zan_set, 16384))
      return;
    shrink_zan_set_ready = true;
  }
  shrink_strset_add(&shrink_zan_set, name);
}

/* native CRT/Win32 精确名（语料实测声明全集；新导入不进表 = 保守
 * MAYTHROW，只损失塌缩不损失正确性）。均为 OS/CRT 函数：其 SEH 为
 * OS 层，__gxx personality 拒收非 C++ code，无 Zan landing pad 可达。 */
static const char *const shrink_native_names[] = {
    "calloc",
    "malloc",
    "free",
    "realloc",
    "exit",
    "fflush",
    "printf",
    "puts",
    "setvbuf",
    "strlen",
    "strncmp",
    "_setmode",
    "__acrt_iob_func",
    "_ZSt13set_terminatePFvvE",
    "SetConsoleCP",
    "SetConsoleOutputCP",
    "AcquireSRWLockExclusive",
    "ReleaseSRWLockExclusive",
    "InitializeSRWLock",
    "GetCurrentThreadId",
    "CancelIoEx",
    "DestroyWindow",
    "GetWindowLongPtrW",
    "SetFocus",
    "SetForegroundWindow",
    "ShowWindow",
    "UpdateWindow",
};

/* declaration 的 nothrow 白名单（严格，宁漏勿错）：
 * - raise 源（RaiseException/__cxa_throw）与重抛助手
 *   __zan_eh_tmp_unwind 恒 MAYTHROW；
 * - Zan 名注册表命中（跨片 Zan 函数）恒 MAYTHROW；
 * - 前缀 __cxa_/__gxx_/llvm.：C++ ABI 支撑与 LLVM 内建；
 * - 前缀 zan_（含 zan_rt_/zan_gui_/zan_dispatch_…）与 __zan_：native
 *   runtime/驱动/编译器助手，纯 C 无 __cxa_*（实测 src/runtime、
 *   src/drivers），其 RaiseException 为 fatal 形态（自定义 SEH code，
 *   personality 拒收，无 landing pad 可达）；registry 先判，Zan 函数
 *   即便撞前缀也判可抛；
 * - CRT/Win32 精确名表。
 * 其余 declaration → MAYTHROW。第一版"C ABI 默认 nothrow"被 IDE GUI
 * smoke 证伪：跨片 Zan 声明被误判，塌缩拆掉了必需的 unwind 边（绿
 * exe 启动即 FileNotFoundException，红 exe 正常起窗）——失败方向必须
 * 是"少塌缩"。 */
static bool shrink_extern_nothrow(const char *n) {
  if (shrink_is_raise_source(n))
    return false;
  if (strcmp(n, "__zan_eh_tmp_unwind") == 0)
    return false;
  if (shrink_strset_has(&shrink_zan_set, n))
    return false;
  if (strncmp(n, "__cxa_", 6) == 0 || strncmp(n, "__gxx_", 6) == 0 ||
      strncmp(n, "llvm.", 5) == 0 || strncmp(n, "zan_", 4) == 0 ||
      strncmp(n, "__zan_", 6) == 0)
    return true;
  for (unsigned i = 0;
       i < sizeof(shrink_native_names) / sizeof(shrink_native_names[0]); i++)
    if (strcmp(n, shrink_native_names[i]) == 0)
      return true;
  return false;
}

/* 调用站点的直接被调者（穿透常量 bitcast 到 Function），无则 NULL */
static LLVMValueRef shrink_callee_fn(LLVMValueRef inst) {
  LLVMValueRef callee = LLVMGetCalledValue(inst);
  if (!callee)
    return NULL;
  for (int d = 0; d < 8 && LLVMIsAConstantExpr(callee) &&
                  LLVMGetConstOpcode(callee) == LLVMBitCast;
       d++) {
    callee = LLVMGetOperand(callee, 0);
  }
  return LLVMIsAFunction(callee) ? callee : NULL;
}

typedef struct {
  unsigned *v;
  unsigned n, cap;
} shrink_uvec_t;

static bool shrink_uvec_push(shrink_uvec_t *u, unsigned x) {
  if (u->n == u->cap) {
    unsigned nc = u->cap ? u->cap * 2 : 8;
    unsigned *nv = (unsigned *)realloc(u->v, nc * sizeof(unsigned));
    if (!nv)
      return false;
    u->v = nv;
    u->cap = nc;
  }
  u->v[u->n++] = x;
  return true;
}

/* fatal raise 形态（账本 §3.2/_lbpr 解剖）：RaiseException → exit →
 * unreachable 紧邻。personality 是 __gxx_personality_v0/seh0，只认
 * C++ ABI 魔数；RaiseException 的自定义 SEH code 不会进任何 Zan
 * landing pad，进程即告终止——因此该形态不构成"可抛"，调用它的
 * 函数对塌缩而言等价 nothrow。非此形态的 RaiseException 站点保守
 * 记 MAYTHROW。 */
static bool shrink_fatal_raise_after(LLVMValueRef raise_call) {
  LLVMValueRef inst = LLVMGetNextInstruction(raise_call);
  if (!inst || LLVMGetInstructionOpcode(inst) != LLVMCall)
    return false;
  LLVMValueRef c = LLVMGetCalledValue(inst);
  if (!c || !LLVMIsAFunction(c) || strcmp(LLVMGetValueName(c), "exit") != 0)
    return false;
  inst = LLVMGetNextInstruction(inst);
  return inst && LLVMGetInstructionOpcode(inst) == LLVMUnreachable;
}
static void shrink_collapse_invoke(LLVMBuilderRef b, LLVMValueRef inv) {
  LLVMValueRef fn = shrink_callee_fn(inv);
  if (!fn)
    return;
  unsigned nparams = LLVMGetNumArgOperands(inv);
  LLVMValueRef *args =
      (LLVMValueRef *)alloca((nparams ? nparams : 1) * sizeof(LLVMValueRef));
  for (unsigned i = 0; i < nparams; i++)
    args[i] = LLVMGetOperand(inv, i);
  LLVMBasicBlockRef norm = LLVMGetNormalDest(inv);
  LLVMBasicBlockRef unw = LLVMGetUnwindDest(inv);

  LLVMPositionBuilderBefore(b, inv);
  /* 站点函数类型：runtime 的 C-ABI int-abi 会给同一符号混用不同函数
   * 类型（如 invoke i64 @calloc vs declare ptr @calloc）——必须用
   * 站点类型 LLVMGetCalledFunctionType，不能用 LLVMGlobalGetValueType
   * 的声明规范类型，否则 RAUW 把 i64 结果静默改成 ptr（下游 icmp/call
   * 全部类型错位）。 */
  LLVMValueRef call =
      LLVMBuildCall2(b, LLVMGetCalledFunctionType(inv), fn, args, nparams, "");
  LLVMSetInstructionCallConv(call, LLVMGetInstructionCallConv(inv));
  for (unsigned idx = 0; idx <= nparams; idx++) {
    unsigned cnt = LLVMGetCallSiteAttributeCount(inv, idx);
    if (!cnt)
      continue;
    LLVMAttributeRef *attrs =
        (LLVMAttributeRef *)alloca(cnt * sizeof(LLVMAttributeRef));
    LLVMGetCallSiteAttributes(inv, idx, attrs);
    for (unsigned a = 0; a < cnt; a++)
      LLVMAddCallSiteAttribute(call, idx, attrs[a]);
  }
  {
    unsigned vcall_md =
        LLVMGetMDKindIDInContext(LLVMGetModuleContext(LLVMGetGlobalParent(fn)),
                                 "zan.vcall", 9);
    LLVMValueRef md = LLVMGetMetadata(inv, vcall_md);
    if (md)
      LLVMSetMetadata(call, vcall_md, md);
  }
  LLVMReplaceAllUsesWith(inv, call);
  LLVMBasicBlockRef blk = LLVMGetInstructionParent(inv);
  /* unwind 目标块的 PHI 仍持有 (值, blk) 入口——RAUW 只修 value 不修
   * CFG 前驱，erase 后该入口成悬空前驱（verifier: PHINode should
   * have one entry for each predecessor）。对齐 LLVM
   * BasicBlock::removePredecessor 语义：C API 无删入口接口，重建
   * PHI 去掉该入口；入口全失（本 invoke 是 unw 唯一前驱）则整块
   * 已不可达，RAUW undef 后删除 PHI。 */
  LLVMValueRef phi = LLVMGetFirstInstruction(unw);
  while (phi && LLVMGetInstructionOpcode(phi) == LLVMPHI) {
    LLVMValueRef next = LLVMGetNextInstruction(phi);
    unsigned n = LLVMCountIncoming(phi);
    unsigned keep = 0, stale_at = n;
    for (unsigned i = 0; i < n; i++) {
      if (LLVMGetIncomingBlock(phi, i) == blk)
        stale_at = i;
      else
        keep++;
    }
    if (stale_at < n) {
      if (keep == 0) {
        /* unw 只剩这一个（已消失的）前驱：块不可达 */
        LLVMReplaceAllUsesWith(phi, LLVMGetUndef(LLVMTypeOf(phi)));
        LLVMInstructionEraseFromParent(phi);
      } else {
        LLVMValueRef *vals =
            (LLVMValueRef *)alloca(keep * sizeof(LLVMValueRef));
        LLVMBasicBlockRef *blks =
            (LLVMBasicBlockRef *)alloca(keep * sizeof(LLVMBasicBlockRef));
        unsigned k = 0;
        for (unsigned i = 0; i < n; i++) {
          if (i == stale_at)
            continue;
          vals[k] = LLVMGetIncomingValue(phi, i);
          blks[k] = LLVMGetIncomingBlock(phi, i);
          k++;
        }
        LLVMPositionBuilderBefore(b, phi);
        LLVMValueRef nphi = LLVMBuildPhi(b, LLVMTypeOf(phi), "");
        LLVMAddIncoming(nphi, vals, blks, keep);
        LLVMReplaceAllUsesWith(phi, nphi);
        LLVMInstructionEraseFromParent(phi);
      }
    }
    phi = next;
  }
  /* 必须用 Erase（真删除）：RemoveFromParent 只摘链不销毁，被摘下的
   * invoke 仍持有 normal/unwind 两个目标块的 use——目标块留下幽灵
   * 前驱（打印器显 <null operand!>，verifier 解引用崩溃）。RAUW 已
   * 先行，此刻 value 无引用，可安全删除。 */
  LLVMInstructionEraseFromParent(inv);
  LLVMPositionBuilderAtEnd(b, blk);
  LLVMBuildBr(b, norm);
}

int zan_shrink_collapse_invokes(LLVMModuleRef mod) {
  unsigned nfn = 0;
  for (LLVMValueRef f = LLVMGetFirstFunction(mod); f;
       f = LLVMGetNextFunction(f))
    nfn++;
  if (nfn == 0)
    return 0;

  shrink_map_t idx; /* Function -> 下标 + 1；0 = 未登记 */
  if (!shrink_map_init(&idx, nfn))
    return 0;
  shrink_map_t st; /* Function -> shrink_state_t */
  if (!shrink_map_init(&st, nfn)) {
    shrink_map_free(&idx);
    return 0;
  }

  LLVMValueRef *fns = (LLVMValueRef *)calloc(nfn, sizeof(LLVMValueRef));
  shrink_uvec_t *callees = (shrink_uvec_t *)calloc(nfn, sizeof(shrink_uvec_t));
  bool *has_raise = (bool *)calloc(nfn, sizeof(bool));
  if (!fns || !callees || !has_raise)
    goto fail;
  {
    unsigned fi = 0;
    for (LLVMValueRef f = LLVMGetFirstFunction(mod); f;
         f = LLVMGetNextFunction(f), fi++) {
      fns[fi] = f;
      bool fresh = false;
      int *slot = shrink_map_slot(&idx, f, &fresh);
      *slot = (int)(fi + 1);
      (void)fresh;
      const char *n = LLVMGetValueName(f);
      *shrink_map_slot(&st, f, NULL) =
          LLVMIsDeclaration(f)
              ? (shrink_is_raise_source(n)
                     ? SHRINK_MAYTHROW
                     : (shrink_extern_nothrow(n) ? SHRINK_NOTHROW
                                                 : SHRINK_MAYTHROW))
              : SHRINK_UNKNOWN;
    }
    /* 边收集：caller -> callee（模块内定义）+ raise 源标记。
     * 模块外被调者按名字规则直接惩罚 caller（保守：未知 extern
     * 视为可抛——宁漏勿错）。 */
    for (unsigned i = 0; i < nfn; i++) {
      LLVMValueRef f = fns[i];
      if (LLVMIsDeclaration(f))
        continue;
      for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(f); bb;
           bb = LLVMGetNextBasicBlock(bb)) {
        for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
             inst = LLVMGetNextInstruction(inst)) {
          unsigned op = LLVMGetInstructionOpcode(inst);
          if (op != LLVMCall && op != LLVMInvoke)
            continue;
          LLVMValueRef c = LLVMGetCalledValue(inst);
          const char *cn = c ? LLVMGetValueName(c) : NULL;
          if (cn && strcmp(cn, "RaiseException") == 0 &&
              shrink_fatal_raise_after(inst))
            continue; /* fatal 形态：不构成可抛 */
          c = shrink_callee_fn(inst);
          if (!c) {
            /* Unknown indirect targets may unwind. Skipping the
             * edge without this mark incorrectly proves wrappers
             * (reflection/delegates/vtables) nothrow. */
            has_raise[i] = true;
            continue;
          }
          bool fresh = false;
          int ci1 = *shrink_map_slot(&idx, c, &fresh);
          if (ci1 == 0) {
            if (!shrink_extern_nothrow(LLVMGetValueName(c)) ||
                shrink_is_raise_source(LLVMGetValueName(c)))
              has_raise[i] = true;
            continue;
          }
          unsigned ci = (unsigned)ci1 - 1;
          if (shrink_is_raise_source(LLVMGetValueName(c)))
            has_raise[i] = true;
          if (!shrink_uvec_push(&callees[i], ci))
            goto oom;
        }
      }
    }
    /* 不动点（round-robin）：nothrow ⇔ 无 raise 源 ∧ 全部被调者
     * nothrow。extern 种子已定，传播至闭包。 */
    bool changed = true;
    while (changed) {
      changed = false;
      for (unsigned i = 0; i < nfn; i++) {
        LLVMValueRef f = fns[i];
        if (LLVMIsDeclaration(f))
          continue;
        int *sp = shrink_map_slot(&st, f, NULL);
        if (*sp == SHRINK_NOTHROW || has_raise[i])
          continue;
        bool all_nt = true;
        for (unsigned k = 0; k < callees[i].n; k++) {
          int cs = *shrink_map_slot(&st, fns[callees[i].v[k]], NULL);
          if (cs != SHRINK_NOTHROW) {
            all_nt = false;
            break;
          }
        }
        if (all_nt) {
          *sp = SHRINK_NOTHROW;
          changed = true;
        }
      }
    }
    /* 塌缩：被调者 nothrow 且可解析为 Function 的 invoke → call+br。
     * 先取后继再迭代（擦除只动 invoke 自己，插入的 call/br 不含
     * invoke，按 next 指针继续安全）。 */
    int collapsed = 0, total_inv = 0, nthr = 0;
    LLVMBuilderRef b = LLVMCreateBuilderInContext(LLVMGetModuleContext(mod));
    for (unsigned i = 0; i < nfn; i++) {
      LLVMValueRef f = fns[i];
      if (LLVMIsDeclaration(f))
        continue;
      for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(f); bb;
           bb = LLVMGetNextBasicBlock(bb)) {
        LLVMValueRef inst = LLVMGetFirstInstruction(bb);
        while (inst) {
          LLVMValueRef next = LLVMGetNextInstruction(inst);
          if (LLVMGetInstructionOpcode(inst) == LLVMInvoke) {
            total_inv++;
            LLVMValueRef c = shrink_callee_fn(inst);
            int cs = c ? *shrink_map_slot(&st, c, NULL) : SHRINK_MAYTHROW;
            if (cs == SHRINK_NOTHROW) {
              /* invoke 无 tail 语义（musttail 仅 CallInst），
               * 无需检查——C API LLVMIsTailCall 会按 CallInst
               * 强转 InvokeInst，不可用。 */
              shrink_collapse_invoke(b, inst);
              collapsed++;
            }
          }
          inst = next;
        }
      }
    }
    for (unsigned i = 0; i < nfn; i++) {
      if (*shrink_map_slot(&st, fns[i], NULL) == SHRINK_NOTHROW)
        nthr++;
    }
    LLVMDisposeBuilder(b);
    zan_opt_verify_module(mod, "after shrink collapse");
    fprintf(stderr,
            "shrink: nounwind: %d/%d invokes collapsed (%d/%u fns nothrow)\n",
            collapsed, total_inv, nthr, nfn);
  }
oom:
fail:
  for (unsigned i = 0; i < nfn; i++)
    free(callees[i].v);
  free(fns);
  free(callees);
  free(has_raise);
  shrink_map_free(&idx);
  shrink_map_free(&st);
  return 0;
}

static void insert_entry_alloca(LLVMBuilderRef b, LLVMValueRef fn) {
  LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
  LLVMValueRef first = LLVMGetFirstInstruction(entry);
  while (first && LLVMIsAAllocaInst(first)) {
    first = LLVMGetNextInstruction(first);
  }
  if (first) {
    LLVMPositionBuilderBefore(b, first);
  } else {
    LLVMPositionBuilderAtEnd(b, entry);
  }
}

static bool check_val_escapes(LLVMValueRef val, LLVMBasicBlockRef alloc_bb,
                              LLVMBasicBlockRef entry_bb, int depth) {
  if (depth > 12)
    return true; /* 保守深度封顶 */
  LLVMValueRef base_val = strip_pointer_casts(val);
  LLVMUseRef u = LLVMGetFirstUse(val);
  while (u) {
    LLVMValueRef user = LLVMGetUser(u);
    if (!user || !LLVMIsAInstruction(user))
      return true;
    LLVMOpcode op = LLVMGetInstructionOpcode(user);

    if (op == LLVMBitCast || op == LLVMAddrSpaceCast ||
        op == LLVMGetElementPtr || op == LLVMSelect) {
      if (check_val_escapes(user, alloc_bb, entry_bb, depth + 1))
        return true;
    } else if (op == LLVMICmp || op == LLVMFCmp) {
      /* 仅比较指针地址（如判空）不逃逸 */
    } else if (op == LLVMLoad) {
      /* 读字段或取值不逃逸基对象 */
    } else if (op == LLVMStore) {
      LLVMValueRef store_val = strip_pointer_casts(LLVMGetOperand(user, 0));
      LLVMValueRef store_dst = strip_pointer_casts(LLVMGetOperand(user, 1));
      if (store_dst == base_val) {
        /* 写入对象自身字段：对象自身不逃逸 */
      } else if (store_val == base_val) {
        /* 对象被存入某目标 */
        if (LLVMIsAAllocaInst(store_dst)) {
          /* 若分配在非入口块（可能位于循环中），存入长寿命栈槽可能跨迭代存活 */
          if (alloc_bb != entry_bb) {
            LLVMUseRef au_chk = LLVMGetFirstUse(store_dst);
            while (au_chk) {
              LLVMValueRef auser_chk = LLVMGetUser(au_chk);
              if (!auser_chk || !LLVMIsAInstruction(auser_chk) ||
                  LLVMGetInstructionParent(auser_chk) != alloc_bb)
                return true;
              au_chk = LLVMGetNextUse(au_chk);
            }
          }
          /* 存入局部栈槽，递归检查对该栈槽的所有使用 */
          LLVMUseRef au = LLVMGetFirstUse(store_dst);
          while (au) {
            LLVMValueRef auser = LLVMGetUser(au);
            if (auser != user) {
              if (!LLVMIsAInstruction(auser))
                return true;
              LLVMOpcode aop = LLVMGetInstructionOpcode(auser);
              if (aop == LLVMLoad) {
                if (check_val_escapes(auser, alloc_bb, entry_bb, depth + 1))
                  return true;
              } else if (aop == LLVMStore) {
                /* 只有覆盖写入 store_dst 才安全；把 store_dst 指针本身作为值存出则逃逸 */
                if (strip_pointer_casts(LLVMGetOperand(auser, 1)) != store_dst)
                  return true;
              } else {
                return true;
              }
            }
            au = LLVMGetNextUse(au);
          }
        } else {
          return true;
        }
      } else {
        return true;
      }
    } else if (op == LLVMCall) {
      if (LLVMGetTailCallKind(user) == LLVMTailCallKindMustTail)
        return true;
      LLVMValueRef callee = LLVMGetCalledValue(user);
      const char *name = callee ? LLVMGetValueName(callee) : NULL;
      if (!name)
        return true;
      if (strcmp(name, "zan_rt_retain") == 0 ||
          strcmp(name, "zan_retain") == 0 ||
          strcmp(name, "zan_rt_release") == 0 ||
          strcmp(name, "zan_rt_release_dyn") == 0 ||
          strcmp(name, "zan_release") == 0 ||
          strcmp(name, "zan_rt_str_retain") == 0 ||
          strcmp(name, "zan_rt_str_release") == 0 ||
          strcmp(name, "zan_rt_arr_len") == 0 ||
          strcmp(name, "zan_bound_check") == 0 ||
          strcmp(name, "zan_rt_bound_check") == 0 ||
          strcmp(name, "zan_rt_live_register") == 0 ||
          strcmp(name, "zan_rt_live_register_named") == 0 ||
          strcmp(name, "zan_rt_live_unregister") == 0 ||
          strncmp(name, "llvm.memset", 11) == 0 ||
          strncmp(name, "llvm.memcpy", 11) == 0 ||
          strncmp(name, "llvm.memmove", 12) == 0 ||
          strncmp(name, "llvm.lifetime", 13) == 0) {
        /* 安全内部调用 */
      } else {
        return true;
      }
    } else if (op == LLVMRet) {
      return true; /* 返回指针则逃逸 */
    } else {
      return true;
    }

    u = LLVMGetNextUse(u);
  }
  return false;
}

/* 栈提升把堆指针替换为 caller 帧内的 alloca；若前置 LLVM pass 已给接收该指针的
 * 外部调用标过 tail，后续 DSE 会依据 tail 不读 caller alloca 的假设把头部 rc/site
 * 存储删掉，必须在替换前清除所有可达调用点上的 tail 标记。 */
static void clear_escape_user_tail_calls(LLVMValueRef val, int depth) {
  if (!val || depth > 12)
    return;
  LLVMValueRef base_val = strip_pointer_casts(val);
  LLVMUseRef u = LLVMGetFirstUse(val);
  while (u) {
    LLVMValueRef user = LLVMGetUser(u);
    if (user && LLVMIsAInstruction(user)) {
      LLVMOpcode op = LLVMGetInstructionOpcode(user);
      if (op == LLVMBitCast || op == LLVMAddrSpaceCast ||
          op == LLVMGetElementPtr || op == LLVMSelect) {
        clear_escape_user_tail_calls(user, depth + 1);
      } else if (op == LLVMStore) {
        LLVMValueRef store_val = strip_pointer_casts(LLVMGetOperand(user, 0));
        LLVMValueRef store_dst = strip_pointer_casts(LLVMGetOperand(user, 1));
        if (store_dst != base_val && store_val == base_val &&
            LLVMIsAAllocaInst(store_dst)) {
          LLVMUseRef au = LLVMGetFirstUse(store_dst);
          while (au) {
            LLVMValueRef auser = LLVMGetUser(au);
            if (auser && auser != user && LLVMIsAInstruction(auser) &&
                LLVMGetInstructionOpcode(auser) == LLVMLoad) {
              clear_escape_user_tail_calls(auser, depth + 1);
            }
            au = LLVMGetNextUse(au);
          }
        }
      } else if (op == LLVMCall) {
        if (LLVMGetTailCallKind(user) == LLVMTailCallKindTail)
          LLVMSetTailCallKind(user, LLVMTailCallKindNone);
      }
    }
    u = LLVMGetNextUse(u);
  }
}

static zan_escape_stats_t zan_opt_escape_mod(LLVMModuleRef mod) {
  zan_escape_stats_t stats = {0, 0};
  if (!mod)
    return stats;

  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
  LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx);

  LLVMValueRef fn = LLVMGetFirstFunction(mod);
  while (fn) {
    if (!LLVMIsDeclaration(fn)) {
      LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(fn);
      LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
      while (bb) {
        LLVMValueRef inst = LLVMGetFirstInstruction(bb);
        while (inst) {
          LLVMValueRef next = LLVMGetNextInstruction(inst);
          if (LLVMGetInstructionOpcode(inst) == LLVMCall) {
            LLVMValueRef callee = LLVMGetCalledValue(inst);
            const char *callee_name = callee ? LLVMGetValueName(callee) : NULL;
            if (callee_name && (strcmp(callee_name, "zan_rt_alloc") == 0 ||
                                strcmp(callee_name, "zan_alloc") == 0)) {
              stats.objects_analyzed++;
              LLVMValueRef size_val = LLVMGetOperand(inst, 0);
              LLVMValueRef site_val = LLVMGetOperand(inst, 1);
              /* 仅对有界常量尺寸做入口栈提升，防止动态大尺寸或循环内动态 alloca 爆栈/破坏 SSA 支配 */
              if (LLVMIsAConstantInt(size_val)) {
                uint64_t obj_sz = LLVMConstIntGetZExtValue(size_val);
                if (obj_sz <= 4096 &&
                    !check_val_escapes(inst, bb, entry_bb, 0)) {
                  LLVMBuilderRef b_entry = LLVMCreateBuilderInContext(ctx);
                  insert_entry_alloca(b_entry, fn);

                  LLVMValueRef total_val =
                      LLVMConstInt(i64, obj_sz + ZAN_OBJ_HDR_SIZE, 0);
                  LLVMValueRef alloca_buf =
                      LLVMBuildArrayAlloca(b_entry, i8, total_val, "stk.raw");
                  LLVMSetAlignment(alloca_buf, 16);
                  LLVMDisposeBuilder(b_entry);

                  LLVMBuilderRef b_inst = LLVMCreateBuilderInContext(ctx);
                  LLVMPositionBuilderBefore(b_inst, inst);

                  /* rc = 1 at raw[0..7] */
                  LLVMValueRef rc_ptr = LLVMBuildBitCast(
                      b_inst, alloca_buf, LLVMPointerType(i64, 0), "stk.rcp");
                  LLVMBuildStore(b_inst, LLVMConstInt(i64, 1, 0), rc_ptr);

                  /* site = site | ZAN_OBJ_STACK_FLAG at raw[8..15] */
                  LLVMValueRef site_marked = LLVMBuildOr(
                      b_inst, site_val,
                      LLVMConstInt(i64, ZAN_OBJ_STACK_FLAG, 0), "stk.site");
                  LLVMValueRef eight =
                      LLVMConstInt(i64, (uint64_t)(-ZAN_OBJ_SITE_OFF), 0);
                  LLVMValueRef site_ptr = LLVMBuildGEP2(
                      b_inst, i8, alloca_buf, &eight, 1, "stk.sptr");
                  LLVMValueRef site_iptr = LLVMBuildBitCast(
                      b_inst, site_ptr, LLVMPointerType(i64, 0), "stk.siptr");
                  LLVMBuildStore(b_inst, site_marked, site_iptr);

                  /* user_ptr = raw + 16 */
                  LLVMValueRef hdr_off =
                      LLVMConstInt(i64, ZAN_OBJ_HDR_SIZE, 0);
                  LLVMValueRef stk_user_ptr = LLVMBuildGEP2(
                      b_inst, i8, alloca_buf, &hdr_off, 1, "stk.usr");
                  LLVMDisposeBuilder(b_inst);

                  clear_escape_user_tail_calls(inst, 0);
                  LLVMReplaceAllUsesWith(inst, stk_user_ptr);
                  LLVMInstructionEraseFromParent(inst);

                  stats.stack_promoted++;
                }
              }
            }
          }
          inst = next;
        }
        bb = LLVMGetNextBasicBlock(bb);
      }
    }
    fn = LLVMGetNextFunction(fn);
  }
  return stats;
}

static zan_status_t zan_opt_run_passes_ex(LLVMModuleRef mod,
                                          LLVMTargetMachineRef tm,
                                          zan_opt_level_t level,
                                          const zan_opt_knobs_t *knobs,
                                          zan_irgen_t *g_opt,
                                          zan_opt_report_t *report_out) {
  static const zan_opt_knobs_t default_knobs;
  if (!knobs)
    knobs = &default_knobs;
  if (zan_opt_verify_module(mod, "before optimization") != ZAN_OK)
    return ZAN_ERROR;
  if (level == ZAN_OPT_NONE)
    return ZAN_OK;
  /* S-UNWIND（全局 pass ②）：在 LLVM
   * 管线前塌缩可证不抛的 invoke，死 lpad 块交给后续管线清理。
   * -fno-opt-shrink（或回退 ZAN_NO_UNWIND_SHRINK=1）关断（红绿对照用）。 */
  if (!zan_opt_knob_off(knobs->no_shrink, "ZAN_NO_UNWIND_SHRINK")) {
    zan_shrink_collapse_invokes(mod);
  }
  const char *passes = NULL;
  switch (level) {
  case ZAN_OPT_BASIC:
    passes = "default<O1>";
    break;
  case ZAN_OPT_FULL:
    passes = "default<O2>";
    break;
#if ZAN_LLVM_MAJOR >= 23
  case ZAN_OPT_SIZE:
    zan_opt_mark_size(mod, false);
    passes = "default<O2>,mergefunc,globaldce";
    break;
  case ZAN_OPT_SIZE_MIN:
    zan_opt_mark_size(mod, true);
    passes = "default<O2>,mergefunc,globaldce";
    break;
#else
  case ZAN_OPT_SIZE:
    passes = "default<Os>,mergefunc,globaldce";
    break;
  case ZAN_OPT_SIZE_MIN:
    passes = "default<Oz>,mergefunc,globaldce";
    break;
#endif
  case ZAN_OPT_AGGRESSIVE:
    passes = "default<O3>";
    break;
  default:
    return ZAN_ERROR;
  }

  /* 调试旋钮：--opt-pipeline（或回退 ZAN_OPT_PIPELINE）覆盖默认管线
   * 文本（如 "sroa" 逐 pass 二分），"none" 完全跳过 LLVM 管线——定位
   * 优化缺陷用（A595）。 */
  bool custom_pipe = false;
  {
    const char *pipe =
        knobs->pipeline ? knobs->pipeline : getenv("ZAN_OPT_PIPELINE");
    if (pipe && pipe[0]) {
      if (strcmp(pipe, "none") == 0) {
        return ZAN_OK;
      }
      passes = pipe;
      custom_pipe = true;
    }
  }

  /* 在主 LLVM O* 管线期间暂时给运行时 ARC/alloc 辅助函数加上 noinline，
   * 让用户函数先行内联并暴露出完整逃逸/ARC 配对/常量特征，再在后续清理中按需放行内联。 */
  static const char *const k_rt_defer_inline[] = {
      "zan_rt_alloc",       "zan_rt_retain",     "zan_rt_release",
      "zan_rt_release_dyn", "zan_rt_str_retain", "zan_rt_str_release",
  };
  LLVMValueRef marked_rt[6] = {0};
  int marked_count = 0;
  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  unsigned noinline_kind = LLVMGetEnumAttributeKindForName("noinline", 8);
  if (!custom_pipe && noinline_kind != 0 && level != ZAN_OPT_BASIC) {
    LLVMAttributeRef noinl_attr = LLVMCreateEnumAttribute(ctx, noinline_kind, 0);
    for (int i = 0; i < 6; i++) {
      LLVMValueRef fn = LLVMGetNamedFunction(mod, k_rt_defer_inline[i]);
      if (fn && !LLVMIsDeclaration(fn) &&
          !LLVMGetEnumAttributeAtIndex(
              fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
              noinline_kind)) {
        LLVMAddAttributeAtIndex(
            fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, noinl_attr);
        marked_rt[marked_count++] = fn;
      }
    }
  }

  LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
  LLVMPassBuilderOptionsSetVerifyEach(opts, 0);
  LLVMPassBuilderOptionsSetDebugLogging(opts, 0);

  if (level == ZAN_OPT_FULL || level == ZAN_OPT_AGGRESSIVE) {
    LLVMPassBuilderOptionsSetLoopInterleaving(opts, 1);
    LLVMPassBuilderOptionsSetLoopVectorization(opts, 1);
    LLVMPassBuilderOptionsSetSLPVectorization(opts, 1);
    LLVMPassBuilderOptionsSetLoopUnrolling(opts, 1);
  }

  LLVMErrorRef err = LLVMRunPasses(mod, passes, tm, opts);
  LLVMDisposePassBuilderOptions(opts);
  if (err) {
    for (int i = 0; i < marked_count; i++) {
      LLVMRemoveEnumAttributeAtIndex(
          marked_rt[i], (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
          noinline_kind);
    }
    char *msg = LLVMGetErrorMessage(err);
    fprintf(stderr, "error: LLVM pass pipeline '%s' failed: %s\n", passes, msg);
    LLVMDisposeErrorMessage(msg);
    return ZAN_ERROR;
  }

  /* Post-inline Zan 优化 pass：主内联与 SROA 完成后再次消除栈逃逸、ARC 冗余、虚调用、常量与死代码 */
  bool post_changed = false;
  if (!custom_pipe) {
    if (!zan_opt_knob_off(knobs->no_escape, "ZAN_NO_OPT_ESCAPE")) {
      zan_escape_stats_t es = zan_opt_escape_mod(mod);
      if (es.stack_promoted > 0)
        post_changed = true;
      if (report_out) {
        report_out->escape.objects_analyzed += es.objects_analyzed;
        report_out->escape.stack_promoted += es.stack_promoted;
      }
    }
    if (!zan_opt_knob_off(knobs->no_arc, "ZAN_NO_OPT_ARC")) {
      zan_arc_opt_stats_t as = zan_opt_arc_mod(mod, level);
      if (as.pairs_elided > 0)
        post_changed = true;
      if (report_out)
        report_out->arc.pairs_elided += as.pairs_elided;
    }
    if (!zan_opt_knob_off(knobs->no_devirt, "ZAN_NO_OPT_DEVIRT")) {
      zan_devirt_stats_t ds = zan_opt_devirt_mod(mod, g_opt);
      if (ds.calls_devirtualized > 0)
        post_changed = true;
      if (report_out)
        report_out->devirt.calls_devirtualized += ds.calls_devirtualized;
    }
    if (!zan_opt_knob_off(knobs->no_constfold, "ZAN_NO_OPT_CONSTFOLD")) {
      zan_constfold_stats_t cs = zan_opt_const_fold_mod(mod);
      if (cs.constants_folded > 0)
        post_changed = true;
      if (report_out)
        report_out->constfold.constants_folded += cs.constants_folded;
    }
    if (!zan_opt_knob_off(knobs->no_dce, "ZAN_NO_OPT_DCE")) {
      zan_dce_stats_t ds2 = zan_opt_dce_mod(mod);
      if (ds2.dead_stores > 0)
        post_changed = true;
      if (report_out)
        report_out->dce.dead_stores += ds2.dead_stores;
    }

    for (int i = 0; i < marked_count; i++) {
      LLVMRemoveEnumAttributeAtIndex(
          marked_rt[i], (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
          noinline_kind);
    }

    const char *cleanup = NULL;
    if (level == ZAN_OPT_FULL || level == ZAN_OPT_AGGRESSIVE) {
      if (post_changed)
        cleanup =
            "function(sroa,early-cse,instcombine<no-verify-fixpoint>,simplifycfg,dse),"
            "globaldce";
    } else if (level == ZAN_OPT_SIZE || level == ZAN_OPT_SIZE_MIN) {
      if (post_changed)
        cleanup =
            "function(sroa,early-cse,instcombine<no-verify-fixpoint>,simplifycfg,dse),mergefunc,"
            "globaldce";
    } else if (post_changed) {
      cleanup =
          "function(sroa,early-cse,instcombine<no-verify-fixpoint>,simplifycfg,dse)";
    }
    if (cleanup && run_checked_passes(mod, tm, cleanup) != ZAN_OK)
      return ZAN_ERROR;
  }

  if (zan_opt_verify_module(mod, "after pass pipeline") != ZAN_OK)
    return ZAN_ERROR;
  /* 调试旋钮：ZAN_DUMP_OPT_IR=<路径> 落盘优化后的模块 IR（A595 二分用） */
  {
    const char *dump = getenv("ZAN_DUMP_OPT_IR");
    if (dump && dump[0]) {
      char *err2 = NULL;
      if (LLVMPrintModuleToFile(mod, dump, &err2)) {
        fprintf(stderr, "warning: dump opt IR failed: %s\n",
                err2 ? err2 : "unknown");
        if (err2)
          LLVMDisposeMessage(err2);
      }
    }
  }

  return ZAN_OK;
}

static zan_status_t zan_opt_configure_llvm_passes_ex(
    zan_irgen_t *g, zan_opt_level_t level, const zan_opt_knobs_t *knobs,
    zan_opt_report_t *report_out) {
  if (level == ZAN_OPT_NONE)
    return ZAN_OK;

  LLVMTargetMachineRef tm = NULL;
  if (zan_irgen_create_target_machine(g, &tm) != ZAN_OK)
    return ZAN_ERROR;
  zan_status_t status =
      zan_opt_run_passes_ex(g->mod, tm, level, knobs, g, report_out);
  if (tm)
    LLVMDisposeTargetMachine(tm);
  return status;
}

zan_status_t zan_opt_configure_llvm_passes(zan_irgen_t *g,
                                           zan_opt_level_t level,
                                           const zan_opt_knobs_t *knobs) {
  return zan_opt_configure_llvm_passes_ex(g, level, knobs, NULL);
}

zan_status_t zan_opt_run_passes_on_module(LLVMModuleRef mod,
                                          LLVMTargetMachineRef tm,
                                          zan_opt_level_t level,
                                          const zan_opt_knobs_t *knobs) {
  return zan_opt_run_passes_ex(mod, tm, level, knobs, NULL, NULL);
}

/* 全局死代码消除 (GlobalDCE)：纯可达性扫描剔除未引用的内部符号 */
zan_status_t zan_opt_strip_unused(zan_irgen_t *g) {
  return run_checked_passes(g->mod, NULL, "globaldce");
}

zan_escape_stats_t zan_opt_escape(zan_irgen_t *g) {
  if (!g || !g->mod) {
    zan_escape_stats_t empty = {0, 0};
    return empty;
  }
  return zan_opt_escape_mod(g->mod);
}

bool zan_opt_knob_off(signed char knob, const char *env) {
  if (knob == 1)
    return true;
  if (knob == 0)
    return getenv(env) != NULL;
  return false;
}

static zan_status_t zan_opt_early_mem2reg(zan_irgen_t *g,
                                          const zan_opt_knobs_t *knobs) {
  const char *early =
      knobs->early_passes ? knobs->early_passes : getenv("ZAN_EARLY_PASSES");
  if (early && strcmp(early, "none") == 0)
    return ZAN_OK;
  return run_checked_passes(g->mod, NULL,
                            (early && early[0]) ? early : "sroa,early-cse");
}

zan_opt_report_t zan_optimize(zan_irgen_t *g, zan_binder_t *binder,
                              zan_opt_level_t level,
                              const zan_opt_knobs_t *knobs) {
  zan_opt_report_t report;
  static const zan_opt_knobs_t default_knobs;
  if (!knobs)
    knobs = &default_knobs;
  memset(&report, 0, sizeof(report));
  if (zan_opt_verify_module(g->mod, "before optimizer") != ZAN_OK)
    return report;
  report.success = true;
  if (level == ZAN_OPT_NONE)
    return report;

  double t0 = get_time_ms();

  /* 调试旋钮：-fno-opt-<name>（或回退 ZAN_NO_OPT_<NAME>=1）跳过对应
   * 自定义 pass（A595 二分） */
  if (!zan_opt_knob_off(knobs->no_early, "ZAN_NO_OPT_EARLY") &&
      zan_opt_early_mem2reg(g, knobs) != ZAN_OK) {
    report.success = false;
    return report;
  }
  if (!zan_opt_knob_off(knobs->no_escape, "ZAN_NO_OPT_ESCAPE"))
    report.escape = zan_opt_escape(g);
  if (!zan_opt_knob_off(knobs->no_arc, "ZAN_NO_OPT_ARC"))
    report.arc = zan_opt_arc(g, level);
  if (!zan_opt_knob_off(knobs->no_devirt, "ZAN_NO_OPT_DEVIRT"))
    report.devirt = zan_opt_devirtualize(g, binder);
  if (!zan_opt_knob_off(knobs->no_constfold, "ZAN_NO_OPT_CONSTFOLD"))
    report.constfold = zan_opt_const_fold(g);
  if (!zan_opt_knob_off(knobs->no_dce, "ZAN_NO_OPT_DCE"))
    report.dce = zan_opt_dce(g);
  if (!zan_opt_knob_off(knobs->no_inline, "ZAN_NO_OPT_INLINE"))
    report.inlining = zan_opt_inline(g, level);
  report.success =
      zan_opt_configure_llvm_passes_ex(g, level, knobs, &report) == ZAN_OK;

  double t1 = get_time_ms();
  report.time_ms = t1 - t0;

  return report;
}

void zan_opt_report_print(const zan_opt_report_t *report) {
  fprintf(stderr, "Optimization report (%.1f ms):\n", report->time_ms);
  if (report->escape.stack_promoted > 0)
    fprintf(stderr, "  Escape: %d objects promoted to stack (of %d analyzed)\n",
            report->escape.stack_promoted, report->escape.objects_analyzed);
  if (report->arc.pairs_elided > 0)
    fprintf(stderr, "  ARC: %d retain/release pairs elided\n",
            report->arc.pairs_elided);
  if (report->devirt.calls_devirtualized > 0)
    fprintf(stderr, "  Devirt: %d virtual calls resolved\n",
            report->devirt.calls_devirtualized);
  if (report->constfold.constants_folded > 0)
    fprintf(stderr, "  Const: %d expressions folded\n",
            report->constfold.constants_folded);
  if (report->dce.dead_stores > 0)
    fprintf(stderr, "  DCE: %d dead stores removed\n", report->dce.dead_stores);
  if (report->inlining.functions_inlined > 0)
    fprintf(stderr, "  Inline: %d functions inlined\n",
            report->inlining.functions_inlined);
}
