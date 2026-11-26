/* build_partition.cpp -- per-source-unit backend modules. See build_partition.h
 * and docs/BUILD_CACHE.md (stage 2). */

#include "build_partition.h"
#include "build_cache.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/TypeFinder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/BLAKE3.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace llvm;

namespace {

/* Below this many instructions in unit bodies the whole module is small
 * enough that splitting costs more than it saves (hello world). */
constexpr long kMinUnitInsns = 20000;
/* available_externally import of another unit's function into a caller's
 * unit: at most this many instructions, decaying per import level (as
 * ThinLTO's import-instr-limit does). */
constexpr unsigned kImportLimit = 100;
/* Residual helpers copied into a unit as internal functions. */
constexpr unsigned kCopyLimit = 60;

double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

unsigned insn_count(const Function &F) {
    unsigned n = 0;
    for (const BasicBlock &BB : F) n += (unsigned)BB.size();
    return n;
}

bool local_linkage(const GlobalValue &GV) { return GV.hasLocalLinkage(); }

bool has_block_address(const Function &F) {
    for (const BasicBlock &BB : F)
        if (BB.hasAddressTaken()) return true;
    return false;
}

/* Every use is the callee of a direct call/invoke (no address escapes). */
bool call_only(const Function &F) {
    for (const Use &U : F.uses()) {
        const auto *CB = dyn_cast<CallBase>(U.getUser());
        if (!CB || !CB->isCallee(&U)) return false;
    }
    return true;
}

/* Visit the global values `V` refers to (through constant expressions and
 * aggregates, and metadata operands). */
template <typename Fn> void for_each_gv_in(const Value *V, DenseSet<const Value *> &seen, Fn &&fn) {
    if (!seen.insert(V).second) return;
    if (const auto *GV = dyn_cast<GlobalValue>(V)) {
        fn(const_cast<GlobalValue *>(GV));
        return;
    }
    if (const auto *C = dyn_cast<Constant>(V)) {
        if (isa<BlockAddress>(C)) {
            fn(const_cast<Function *>(cast<BlockAddress>(C)->getFunction()));
            return;
        }
        for (const Use &Op : C->operands()) for_each_gv_in(Op.get(), seen, fn);
    }
}

template <typename Fn>
void for_each_gv_in_md(const MDNode *N, DenseSet<const Value *> &seen,
                       DenseSet<const MDNode *> &seen_md, Fn &&fn) {
    if (!N || !seen_md.insert(N).second) return;
    for (const MDOperand &Op : N->operands()) {
        const Metadata *MD = Op.get();
        if (!MD) continue;
        if (const auto *VAM = dyn_cast<ValueAsMetadata>(MD))
            for_each_gv_in(VAM->getValue(), seen, fn);
        else if (const auto *Sub = dyn_cast<MDNode>(MD)) for_each_gv_in_md(Sub, seen, seen_md, fn);
    }
}

template <typename Fn> void for_each_gv_of_function(const Function &F, Fn &&fn) {
    DenseSet<const Value *> seen;
    DenseSet<const MDNode *> seen_md;
    if (F.hasPersonalityFn()) for_each_gv_in(F.getPersonalityFn(), seen, fn);
    SmallVector<std::pair<unsigned, MDNode *>, 4> mds;
    for (const Instruction &I : instructions(F)) {
        for (const Use &Op : I.operands()) {
            const Value *V = Op.get();
            if (isa<GlobalValue>(V) || isa<Constant>(V)) for_each_gv_in(V, seen, fn);
            else if (const auto *MAV = dyn_cast<MetadataAsValue>(V)) {
                if (const auto *VAM = dyn_cast<ValueAsMetadata>(MAV->getMetadata()))
                    for_each_gv_in(VAM->getValue(), seen, fn);
                else if (const auto *N = dyn_cast<MDNode>(MAV->getMetadata()))
                    for_each_gv_in_md(N, seen, seen_md, fn);
            }
        }
        mds.clear();
        I.getAllMetadata(mds);
        for (auto &kv : mds) for_each_gv_in_md(kv.second, seen, seen_md, fn);
    }
    mds.clear();
    F.getAllMetadata(mds);
    for (auto &kv : mds) for_each_gv_in_md(kv.second, seen, seen_md, fn);
}

template <typename Fn> void for_each_gv_of_global(const GlobalVariable &G, Fn &&fn) {
    DenseSet<const Value *> seen;
    if (G.hasInitializer()) for_each_gv_in(G.getInitializer(), seen, fn);
}

/* An unnamed_addr constant: any number of private copies is equivalent to
 * the one definition. */
bool copyable_const(const GlobalVariable &G) {
    return G.isConstant() && G.hasGlobalUnnamedAddr() && local_linkage(G) && G.hasInitializer() &&
           !G.hasSection() && !G.isThreadLocal() && !G.isExternallyInitialized() && !G.hasComdat();
}

/* Content name of a copied constant: hash of its type and initializer, where
 * a referenced global contributes its name (or, for another copied
 * constant, its own content name). */
class const_namer {
    DenseMap<const GlobalVariable *, std::string> memo_;

    void hash_const(BLAKE3 &h, const Constant *C, int depth) {
        std::string ty;
        raw_string_ostream os(ty);
        C->getType()->print(os);
        os.flush();
        h.update(ty);
        if (depth > 64) return;
        if (const auto *GV = dyn_cast<GlobalValue>(C)) {
            const auto *G = dyn_cast<GlobalVariable>(GV);
            h.update((G && copyable_const(*G)) ? name(G) : GV->getName().str());
            return;
        }
        if (const auto *CDS = dyn_cast<ConstantDataSequential>(C)) {
            h.update(CDS->getRawDataValues());
            return;
        }
        if (const auto *CI = dyn_cast<ConstantInt>(C)) {
            h.update(toString(CI->getValue(), 16, false));
            return;
        }
        if (const auto *CF = dyn_cast<ConstantFP>(C)) {
            h.update(toString(CF->getValueAPF().bitcastToAPInt(), 16, false));
            return;
        }
        if (const auto *CE = dyn_cast<ConstantExpr>(C)) {
            h.update(std::to_string(CE->getOpcode()));
            if (const auto *GEP = dyn_cast<GEPOperator>(CE)) {
                std::string st;
                raw_string_ostream sos(st);
                GEP->getSourceElementType()->print(sos);
                sos.flush();
                h.update(st);
            }
        }
        h.update(std::to_string(C->getValueID()));
        for (const Use &Op : C->operands()) {
            h.update(",");
            hash_const(h, cast<Constant>(Op.get()), depth + 1);
        }
    }

  public:
    const std::string &name(const GlobalVariable *G) {
        auto it = memo_.find(G);
        if (it != memo_.end()) return it->second;
        memo_[G] = "__zan.k.cycle";
        BLAKE3 h;
        h.update(std::to_string(G->getAlign() ? G->getAlign()->value() : 0));
        hash_const(h, G->getInitializer(), 0);
        auto r = h.final();
        std::string n = "__zan.k." + toHex(ArrayRef<uint8_t>(r.data(), 12), true);
        return memo_[G] = n;
    }
};

/* ---- stable names ----
 * irgen numbers many synthesized symbols by creation order (__zan_desc_N,
 * lambda_N, Foo_ctor.N, __zan.refl.type.N): an unrelated edit renumbers
 * them, and every unit that mentions one would miss the cache. Locals with
 * such a counter suffix are renamed to their base plus a structural content
 * hash, refined twice so a symbol's hash also covers the counter-named
 * symbols it refers to (as Weisfeiler-Lehman refinement does). Equal hashes
 * fall back to a module-order index. */

bool counter_base(StringRef n, StringRef &base) {
    size_t end = n.size();
    bool any = false;
    for (;;) {
        size_t i = end;
        while (i > 0 && isDigit(n[i - 1])) i--;
        if (i == end || i == 0 || (n[i - 1] != '.' && n[i - 1] != '_')) break;
        end = i - 1;
        any = true;
    }
    if (!any || end == 0) return false;
    base = n.substr(0, end);
    return true;
}

class content_hasher {
    DenseMap<const GlobalValue *, std::string> prev_; /* previous round's tokens */
    DenseSet<const GlobalValue *> counted_;
    DenseMap<const Type *, std::string> type_text_;
    DenseMap<const Constant *, std::string> const_memo_;

    const std::string &type_text(Type *T) {
        auto it = type_text_.find(T);
        if (it != type_text_.end()) return it->second;
        std::string s;
        raw_string_ostream os(s);
        T->print(os);
        os.flush();
        return type_text_[T] = s;
    }

  public:
    void set_counted(const DenseSet<const GlobalValue *> &c) { counted_ = c; }
    void set_prev(DenseMap<const GlobalValue *, std::string> &&p) {
        prev_ = std::move(p);
        const_memo_.clear();
    }

    std::string token(const GlobalValue *GV) {
        if (!counted_.count(GV)) return GV->getName().str();
        auto it = prev_.find(GV);
        return it != prev_.end() ? it->second : "#";
    }

    std::string konst(const Constant *C, int depth = 0) {
        auto it = const_memo_.find(C);
        if (it != const_memo_.end()) return it->second;
        std::string out = type_text(C->getType());
        if (const auto *GV = dyn_cast<GlobalValue>(C)) {
            out += "@" + token(GV);
        } else if (const auto *CDS = dyn_cast<ConstantDataSequential>(C)) {
            StringRef raw = CDS->getRawDataValues();
            BLAKE3 h;
            h.update(raw);
            auto r = h.final();
            out += "d" + toHex(ArrayRef<uint8_t>(r.data(), 8), true);
        } else if (const auto *CI = dyn_cast<ConstantInt>(C)) {
            out += "i" + toString(CI->getValue(), 16, false);
        } else if (const auto *CF = dyn_cast<ConstantFP>(C)) {
            out += "f" + toString(CF->getValueAPF().bitcastToAPInt(), 16, false);
        } else {
            out += "v" + std::to_string(C->getValueID());
            if (const auto *CE = dyn_cast<ConstantExpr>(C)) {
                out += "o" + std::to_string(CE->getOpcode());
                if (const auto *GEP = dyn_cast<GEPOperator>(CE))
                    out += type_text(GEP->getSourceElementType());
            }
            if (depth < 32) {
                out += "(";
                for (const Use &Op : C->operands())
                    out += konst(cast<Constant>(Op.get()), depth + 1) + ",";
                out += ")";
            }
        }
        if (out.size() > 256) { /* keep memo entries small */
            BLAKE3 h;
            h.update(out);
            auto r = h.final();
            out = "H" + toHex(ArrayRef<uint8_t>(r.data(), 16), true);
        }
        return const_memo_[C] = out;
    }

    std::string of(const GlobalValue *GV) {
        BLAKE3 h;
        if (const auto *G = dyn_cast<GlobalVariable>(GV)) {
            h.update(type_text(G->getValueType()));
            h.update(G->isConstant() ? "c" : "g");
            if (G->hasInitializer()) h.update(konst(G->getInitializer()));
        } else if (const auto *F = dyn_cast<Function>(GV)) {
            h.update(type_text(F->getFunctionType()));
            DenseMap<const Value *, unsigned> num;
            unsigned next = 0;
            for (const Argument &A : F->args()) num[&A] = next++;
            for (const BasicBlock &BB : *F) {
                num[&BB] = next++;
                for (const Instruction &I : BB) num[&I] = next++;
            }
            std::string line;
            for (const Instruction &I : instructions(*F)) {
                line = std::to_string(I.getOpcode()) + ":" + type_text(I.getType());
                if (const auto *Cmp = dyn_cast<CmpInst>(&I))
                    line += "p" + std::to_string(Cmp->getPredicate());
                if (const auto *GEP = dyn_cast<GetElementPtrInst>(&I))
                    line += type_text(GEP->getSourceElementType());
                if (const auto *AI = dyn_cast<AllocaInst>(&I))
                    line += type_text(AI->getAllocatedType());
                if (const auto *CB = dyn_cast<CallBase>(&I))
                    line += type_text(CB->getFunctionType());
                for (const Use &Op : I.operands()) {
                    const Value *V = Op.get();
                    auto nit = num.find(V);
                    if (nit != num.end()) line += ",%" + std::to_string(nit->second);
                    else if (const auto *C = dyn_cast<Constant>(V)) line += "," + konst(C);
                    else if (const auto *IA = dyn_cast<InlineAsm>(V))
                        line += ",asm:" + StringRef(IA->getAsmString()).str();
                    else line += ",?";
                }
                line += ";";
                h.update(line);
            }
        }
        auto r = h.final();
        return toHex(ArrayRef<uint8_t>(r.data(), 8), true);
    }
};

void stabilize_names(Module &M) {
    std::vector<GlobalValue *> counted;
    DenseSet<const GlobalValue *> cset;
    std::vector<std::string> bases;
    for (GlobalValue &GV : M.global_values()) {
        StringRef base;
        if (!GV.hasLocalLinkage() || GV.isDeclaration() || isa<GlobalAlias>(GV) ||
            isa<GlobalIFunc>(GV) || !counter_base(GV.getName(), base))
            continue;
        counted.push_back(&GV);
        cset.insert(&GV);
        bases.push_back(base.str());
    }
    if (counted.empty()) return;
    content_hasher ch;
    ch.set_counted(cset);
    DenseMap<const GlobalValue *, std::string> tok;
    for (int round = 0; round < 3; round++) {
        DenseMap<const GlobalValue *, std::string> next;
        for (size_t i = 0; i < counted.size(); i++)
            next[counted[i]] = bases[i] + ".h" + ch.of(counted[i]);
        tok = next;
        ch.set_prev(std::move(next));
    }
    /* Free the old names first so a new name never collides with one. */
    for (GlobalValue *GV : counted) GV->setName("");
    std::map<std::string, int> seen;
    for (GlobalValue *GV : counted) {
        std::string n = tok[GV];
        int k = seen[n]++;
        if (k) n += "." + std::to_string(k);
        GV->setName(n);
    }
}

enum member_kind { MK_DEF, MK_IMPORT, MK_COPY, MK_DECL };

struct unit_plan_t {
    std::string name;
    bool user = false;
    std::vector<Function *> defs;
    /* Everything the unit module contains besides its definitions. */
    std::map<GlobalValue *, member_kind> extra;
    long insns = 0;
};

struct planner_t {
    Module &M;
    std::vector<unit_plan_t> units;
    DenseMap<const Function *, int> fn_unit; /* moved definitions */
    DenseSet<const GlobalValue *> promote; /* residual locals others reference */
    DenseSet<const Function *> promote_user; /* user-unit defs others reference */
    DenseMap<const Function *, unsigned> sizes;
    const_namer cnames;
    std::string fail;

    explicit planner_t(Module &m) : M(m) {}

    unsigned size(const Function *F) {
        auto it = sizes.find(F);
        if (it != sizes.end()) return it->second;
        return sizes[F] = insn_count(*F);
    }

    bool movable(const Function &F, const DenseSet<const GlobalValue *> &used) {
        return !F.isDeclaration() && local_linkage(F) && !F.hasComdat() && !F.hasSection() &&
               !F.hasPrefixData() && !F.hasPrologueData() && !used.count(&F) &&
               !has_block_address(F);
    }

    void assign(const zan_part_opts_t *o) {
        DenseSet<const GlobalValue *> used;
        SmallVector<GlobalValue *, 16> uv;
        collectUsedGlobalVariables(M, uv, false);
        collectUsedGlobalVariables(M, uv, true);
        for (GlobalValue *GV : uv) used.insert(GV);

        std::map<std::string, int> by_name;
        for (int i = 0; i < o->unit_count; i++) {
            unit_plan_t u;
            u.name = o->units[i];
            u.user = u.name.rfind("user:", 0) == 0;
            units.push_back(std::move(u));
        }
        for (int i = 0; i < o->fn_count; i++) {
            Function *F = M.getFunction(o->fn_names[i]);
            int u = o->fn_units[i];
            if (!F || u < 0 || u >= o->unit_count || !movable(*F, used)) continue;
            fn_unit[F] = u;
        }
        /* Unnamed helpers (lambdas, async resume parts, ...) follow the one
         * unit all their users belong to. Compiler/runtime helpers stay in
         * the residual, which they are shared through. */
        bool changed = true;
        for (int round = 0; changed && round < 16; round++) {
            changed = false;
            for (Function &F : M) {
                if (fn_unit.count(&F) || !movable(F, used)) continue;
                StringRef n = F.getName();
                if (n.starts_with("__zan") || n.starts_with("zan_") || n.starts_with("llvm."))
                    continue;
                int only = -1;
                bool ok = true;
                for (const User *U : F.users()) {
                    int p = user_unit(U, &F, 0);
                    if (p == -3) continue; /* self */
                    if (p < 0 || (only >= 0 && p != only)) {
                        ok = false;
                        break;
                    }
                    only = p;
                }
                if (ok && only >= 0) {
                    fn_unit[&F] = only;
                    changed = true;
                }
            }
        }
        for (auto &kv : fn_unit) units[kv.second].defs.push_back(const_cast<Function *>(kv.first));
        for (unit_plan_t &u : units) {
            std::sort(u.defs.begin(), u.defs.end(),
                      [](Function *a, Function *b) { return a->getName() < b->getName(); });
            for (Function *F : u.defs) u.insns += size(F);
        }
    }

    /* Unit of the function a use sits in; -3 = inside `self`, -1 = residual
     * or global initializer, -2 = an unassigned candidate (undecided). */
    int user_unit(const User *U, const Function *self, int depth) {
        if (const auto *I = dyn_cast<Instruction>(U)) {
            const Function *P = I->getFunction();
            if (P == self) return -3;
            auto it = fn_unit.find(P);
            return it == fn_unit.end() ? -1 : it->second;
        }
        if (isa<Constant>(U) && !isa<GlobalValue>(U) && depth < 4) {
            int only = -3;
            for (const User *UU : U->users()) {
                int p = user_unit(UU, self, depth + 1);
                if (p == -3) continue;
                if (p < 0 || (only >= 0 && p != only)) return -1;
                only = p;
            }
            return only;
        }
        return -1;
    }

    /* Library unit functions are visible to every other module; user unit
     * functions only when something outside uses them. */
    bool importable(const Function *F, int from, int into, unsigned limit) {
        /* A library unit never depends on user code. */
        if (into == from || (units[from].user && !units[into].user)) return false;
        if (F->hasFnAttribute(Attribute::NoInline) || F->isVarArg()) return false;
        return size(F) <= limit || F->hasFnAttribute(Attribute::AlwaysInline);
    }

    bool copyable_fn(const Function *F) {
        return !F->isDeclaration() && local_linkage(*F) && !F->hasComdat() && !F->hasSection() &&
               !has_block_address(*F) && call_only(*F) && !F->isVarArg() &&
               (size(F) <= kCopyLimit || F->hasFnAttribute(Attribute::AlwaysInline));
    }

    void plan_unit(int ui) {
        unit_plan_t &u = units[ui];
        struct item {
            GlobalValue *gv;
            unsigned budget;
        };
        std::vector<item> work;
        for (Function *F : u.defs) work.push_back({F, kImportLimit});
        auto visit = [&](GlobalValue *GV, unsigned budget) {
            if (isa<GlobalAlias>(GV) || isa<GlobalIFunc>(GV)) {
                fail = "alias";
                return;
            }
            if (auto *F = dyn_cast<Function>(GV)) {
                auto it = fn_unit.find(F);
                if (it != fn_unit.end()) {
                    int from = it->second;
                    if (from == ui) return;
                    if (u.extra.count(F)) return;
                    if (importable(F, from, ui, budget)) {
                        u.extra[F] = MK_IMPORT;
                        if (units[from].user) promote_user.insert(F);
                        work.push_back({F, budget * 7 / 10});
                    } else {
                        u.extra[F] = MK_DECL;
                        if (units[from].user) promote_user.insert(F);
                    }
                    return;
                }
                if (u.extra.count(F)) return;
                if (F->isDeclaration() || F->isIntrinsic()) {
                    u.extra[F] = MK_DECL;
                } else if (copyable_fn(F)) {
                    u.extra[F] = MK_COPY;
                    work.push_back({F, budget});
                } else if (F->hasExternalLinkage() && !F->hasComdat() && !F->isVarArg() &&
                           !has_block_address(*F) && size(F) <= budget &&
                           !F->hasFnAttribute(Attribute::NoInline)) {
                    u.extra[F] = MK_IMPORT;
                    work.push_back({F, budget * 7 / 10});
                } else {
                    u.extra[F] = MK_DECL;
                    if (local_linkage(*F)) promote.insert(F);
                }
                return;
            }
            auto *G = cast<GlobalVariable>(GV);
            if (u.extra.count(G)) return;
            if (copyable_const(*G)) {
                u.extra[G] = MK_COPY;
                work.push_back({G, budget});
                return;
            }
            u.extra[G] = MK_DECL;
            if (local_linkage(*G) && !G->isDeclaration()) promote.insert(G);
        };
        while (!work.empty() && fail.empty()) {
            item it = work.back();
            work.pop_back();
            if (auto *F = dyn_cast<Function>(it.gv))
                for_each_gv_of_function(*F, [&](GlobalValue *GV) { visit(GV, it.budget); });
            else
                for_each_gv_of_global(*cast<GlobalVariable>(it.gv),
                                      [&](GlobalValue *GV) { visit(GV, it.budget); });
        }
    }

    /* Moved user functions the residual still refers to. */
    void plan_residual_refs() {
        for (unit_plan_t &u : units) {
            if (!u.user) continue;
            for (Function *F : u.defs)
                for (const User *U : F->users())
                    if (user_unit(U, nullptr, 0) != fn_unit[F]) {
                        promote_user.insert(F);
                        break;
                    }
        }
    }
};

std::string promoted_name(StringRef n) { return (n + ".zp").str(); }

void anonymize_struct_types(Module &m) {
    TypeFinder tf;
    tf.run(m, /*onlyNamed=*/true);
    for (StructType *st : tf) st->setName("");
}

/* Builds the unit module (in M's context) and returns its bitcode. */
bool build_unit(planner_t &pl, int ui, SmallVector<char, 0> &out) {
    Module &M = pl.M;
    unit_plan_t &u = pl.units[ui];
    auto NM = std::make_unique<Module>("zan-unit", M.getContext());
    NM->setTargetTriple(M.getTargetTriple());
    NM->setDataLayout(M.getDataLayout());
    NM->setModuleInlineAsm(M.getModuleInlineAsm());
    SmallVector<Module::ModuleFlagEntry, 8> flags;
    M.getModuleFlagsMetadata(flags);
    for (auto &f : flags) NM->addModuleFlag(f.Behavior, f.Key->getString(), f.Val);

    struct member {
        std::string name;
        GlobalValue *gv;
        member_kind kind;
    };
    std::vector<member> members;
    for (Function *F : u.defs) members.push_back({F->getName().str(), F, MK_DEF});
    for (auto &kv : u.extra) {
        GlobalValue *GV = kv.first;
        std::string n = GV->getName().str();
        if (kv.second == MK_COPY && isa<GlobalVariable>(GV))
            n = pl.cnames.name(cast<GlobalVariable>(GV));
        members.push_back({n, GV, kv.second});
    }
    std::sort(members.begin(), members.end(),
              [](const member &a, const member &b) { return a.name < b.name; });

    ValueToValueMapTy VMap;
    std::vector<std::pair<GlobalValue *, member *>> made;
    std::map<std::string, GlobalValue *> by_name;
    for (member &m : members) {
        auto dup = by_name.find(m.name);
        if (dup != by_name.end()) { /* two identical copied constants */
            VMap[m.gv] = dup->second;
            continue;
        }
        GlobalValue *NG;
        if (auto *F = dyn_cast<Function>(m.gv)) {
            Function *NF = Function::Create(F->getFunctionType(), GlobalValue::ExternalLinkage,
                                            F->getAddressSpace(), m.name, NM.get());
            NF->copyAttributesFrom(F);
            NF->setComdat(nullptr);
            NG = NF;
        } else {
            auto *G = cast<GlobalVariable>(m.gv);
            auto *NV = new GlobalVariable(*NM, G->getValueType(), G->isConstant(),
                                          GlobalValue::ExternalLinkage, nullptr, m.name, nullptr,
                                          G->getThreadLocalMode(), G->getAddressSpace(),
                                          G->isExternallyInitialized());
            NV->copyAttributesFrom(G);
            NV->setComdat(nullptr);
            NG = NV;
        }
        if (NG->getName() != m.name) return false;
        VMap[m.gv] = NG;
        by_name[m.name] = NG;
        made.push_back({NG, &m});
    }
    for (auto &pr : made) {
        member &m = *pr.second;
        if (m.kind == MK_DECL) continue;
        if (auto *F = dyn_cast<Function>(m.gv)) {
            auto *NF = cast<Function>(pr.first);
            auto ai = NF->arg_begin();
            for (const Argument &A : F->args()) VMap[&A] = &*ai++;
            SmallVector<ReturnInst *, 8> rets;
            CloneFunctionInto(NF, F, VMap, CloneFunctionChangeType::DifferentModule, rets);
        } else {
            auto *G = cast<GlobalVariable>(m.gv);
            cast<GlobalVariable>(pr.first)->setInitializer(MapValue(G->getInitializer(), VMap));
        }
    }
    /* A symbol this program defines itself (moved or promoted): hidden, so
     * references bind directly instead of through the GOT. */
    auto ours = [&](GlobalValue *GV) {
        auto *F = dyn_cast<Function>(GV);
        return pl.promote.count(GV) || (F && pl.fn_unit.count(F));
    };
    /* Linkage last: cloning copies attributes (visibility) from the source. */
    for (auto &pr : made) {
        member &m = *pr.second;
        GlobalValue *NG = pr.first;
        auto hide = [&] {
            NG->setVisibility(GlobalValue::HiddenVisibility);
            NG->setDSOLocal(true);
        };
        switch (m.kind) {
            case MK_DEF: {
                auto *F = cast<Function>(m.gv);
                if (u.user && !pl.promote_user.count(F)) {
                    NG->setLinkage(GlobalValue::InternalLinkage);
                    NG->setVisibility(GlobalValue::DefaultVisibility);
                } else {
                    NG->setLinkage(GlobalValue::ExternalLinkage);
                    hide();
                }
                break;
            }
            case MK_IMPORT:
                NG->setLinkage(GlobalValue::AvailableExternallyLinkage);
                if (ours(m.gv)) hide();
                break;
            case MK_COPY:
                NG->setLinkage(isa<Function>(m.gv) ? GlobalValue::InternalLinkage
                                                   : GlobalValue::PrivateLinkage);
                NG->setVisibility(GlobalValue::DefaultVisibility);
                break;
            case MK_DECL:
                NG->setLinkage(GlobalValue::ExternalLinkage);
                if (auto *NF = dyn_cast<Function>(NG)) NF->setPersonalityFn(nullptr);
                if (ours(m.gv)) hide();
                break;
        }
    }
    std::string err;
    raw_string_ostream es(err);
    if (verifyModule(*NM, &es)) {
        es.flush();
        pl.fail = "unit module " + u.name + " does not verify: " + err.substr(0, 400);
        return false;
    }
    raw_svector_ostream os(out);
    WriteBitcodeToFile(*NM, os);
    return true;
}

struct job_t {
    const Target *target;
    Triple triple;
    std::string cpu, features;
    TargetOptions options;
    Reloc::Model reloc;
    CodeModel::Model code_model;
    CodeGenOptLevel opt_level;
    LLVMTargetMachineRef tm_ref;
    const zan_part_opts_t *opts;
    std::vector<SmallVector<char, 0>> raw;
    std::vector<std::string> paths;
    std::vector<std::string> unit_names;
    std::vector<std::vector<std::string>> refs;
    std::atomic<int> next{0}, hits{0}, misses{0};
    std::mutex mu; /* the build cache is single-threaded */
    std::string error;

    void fail(const std::string &m) {
        std::lock_guard<std::mutex> l(mu);
        if (error.empty()) error = m;
    }

    std::unique_ptr<Module> parse(LLVMContext &ctx, const SmallVector<char, 0> &bc) {
        ctx.setDiscardValueNames(true);
        auto m =
            parseBitcodeFile(MemoryBufferRef(StringRef(bc.data(), bc.size()), "zan-unit"), ctx);
        if (!m) {
            fail("cannot reload unit module: " + toString(m.takeError()));
            return nullptr;
        }
        return std::move(*m);
    }

    void one(int i) {
        /* Canonical form: no value or type names, so nothing but content
         * (and global names) reaches the key or the code generator. */
        SmallVector<char, 0> bc;
        {
            LLVMContext ctx;
            auto m = parse(ctx, raw[i]);
            if (!m) return;
            SmallVector<char, 0>().swap(raw[i]);
            anonymize_struct_types(*m);
            raw_svector_ostream os(bc);
            WriteBitcodeToFile(*m, os);
        }
        if (const char *dir = getenv("ZAN_PART_DUMP"); dir && *dir) {
            /* Debug: each unit's canonical module, to diff two builds. */
            LLVMContext dctx;
            if (auto dm = parse(dctx, bc)) {
                std::error_code ec;
                raw_fd_ostream os(
                    std::string(dir) + "/" + std::to_string(i) + "-" + unit_names[i] + ".ll", ec);
                if (!ec) dm->print(os, nullptr);
            }
        }
        zan_bcache_key_t key = {{0}, false};
        {
            std::lock_guard<std::mutex> l(mu);
            char *cached = nullptr;
            if (zan_bcache_unit_key(bc.data(), bc.size(), tm_ref, opts->level, &key) &&
                zan_bcache_unit_load(&key, paths[i].c_str(), &cached)) {
                SmallVector<StringRef, 64> names;
                StringRef(cached).split(names, '\n', -1, false);
                for (StringRef n : names) refs[i].push_back(n.str());
                free(cached);
                hits++;
                return;
            }
        }
        misses++;
        LLVMContext ctx;
        auto m = parse(ctx, bc);
        if (!m) return;
#if LLVM_VERSION_MAJOR >= 21
        std::unique_ptr<TargetMachine> tm(target->createTargetMachine(
            triple, cpu, features, options, reloc, code_model, opt_level));
#else
        std::unique_ptr<TargetMachine> tm(target->createTargetMachine(
            triple.str(), cpu, features, options, reloc, code_model, opt_level));
#endif
        if (!tm) {
            fail("cannot create a target machine for a unit module");
            return;
        }
        if (opts->optimize(wrap(m.get()), reinterpret_cast<LLVMTargetMachineRef>(tm.get()),
                           opts->level) != 0) {
            fail("optimizing a unit module failed");
            return;
        }
        std::string list;
        for (GlobalValue &GV : m->global_values()) {
            if (!(GV.isDeclaration() || GV.hasAvailableExternallyLinkage()) || GV.use_empty())
                continue;
            if (auto *F = dyn_cast<Function>(&GV); F && F->isIntrinsic()) continue;
            refs[i].push_back(GV.getName().str());
            list += GV.getName().str();
            list += '\n';
        }
        if (opts->prepare) opts->prepare(opts->prepare_ctx, wrap(m.get()));
        {
            std::error_code ec;
            raw_fd_ostream out(paths[i], ec, sys::fs::OF_None);
            if (ec) {
                fail("cannot open " + paths[i] + ": " + ec.message());
                return;
            }
            legacy::PassManager pm;
            if (tm->addPassesToEmitFile(pm, out, nullptr, CodeGenFileType::ObjectFile)) {
                fail("target cannot emit object files");
                return;
            }
            pm.run(*m);
            out.flush();
            if (out.has_error()) {
                fail("write failed: " + paths[i]);
                return;
            }
        }
        std::lock_guard<std::mutex> l(mu);
        zan_bcache_unit_store(&key, paths[i].c_str(), list.c_str());
    }

    std::vector<int> order; /* largest unit first: shortest makespan */

    void worker() {
        for (int k; (k = next.fetch_add(1)) < (int)order.size();) one(order[k]);
    }
};

} // namespace

extern "C" int zan_part_run(LLVMModuleRef mod_ref, LLVMTargetMachineRef tm_ref,
                            const zan_part_opts_t *o, zan_part_result_t *out, char **error) {
    *error = nullptr;
    memset(out, 0, sizeof(*out));
    Module &M = *unwrap(mod_ref);
    double t0 = now_ms();
    planner_t pl(M);
    pl.assign(o);
    long total = 0;
    int live = 0;
    for (unit_plan_t &u : pl.units) {
        total += u.insns;
        if (!u.defs.empty()) live++;
    }
    long min = kMinUnitInsns;
    if (const char *e = getenv("ZAN_PART_MIN_INSNS"); e && *e) min = atol(e);
    out->moved_insns = total;
    if (total < min || live == 0) return 0;
    if (o->prelude) o->prelude(mod_ref);
    stabilize_names(M);
    for (int i = 0; i < (int)pl.units.size() && pl.fail.empty(); i++)
        if (!pl.units[i].defs.empty()) pl.plan_unit(i);
    pl.plan_residual_refs();
    if (!pl.fail.empty()) return 0;

    /* Residual locals a unit refers to become hidden externals, renamed so
     * they cannot collide with a runtime symbol of the same name. */
    std::vector<GlobalValue *> prom;
    for (const GlobalValue *GV : pl.promote) prom.push_back(const_cast<GlobalValue *>(GV));
    std::sort(prom.begin(), prom.end(),
              [](GlobalValue *a, GlobalValue *b) { return a->getName() < b->getName(); });
    for (GlobalValue *GV : prom) {
        GV->setName(promoted_name(GV->getName()));
        GV->setLinkage(GlobalValue::ExternalLinkage);
        GV->setVisibility(GlobalValue::HiddenVisibility);
        GV->setDSOLocal(true);
    }

    job_t job;
    std::vector<int> unit_of_job;
    for (int i = 0; i < (int)pl.units.size(); i++) {
        if (pl.units[i].defs.empty()) continue;
        job.raw.emplace_back();
        if (!build_unit(pl, i, job.raw.back())) {
            *error = strdup(pl.fail.empty() ? "cannot build a unit module" : pl.fail.c_str());
            return -1;
        }
        unit_of_job.push_back(i);
        std::string un = pl.units[i].name;
        for (char &c : un)
            if (c == '/' || c == ':') c = '_';
        job.unit_names.push_back(un);
    }
    /* The residual keeps only declarations of what moved. */
    for (unit_plan_t &u : pl.units)
        for (Function *F : u.defs) {
            F->deleteBody();
            F->setComdat(nullptr);
            F->setLinkage(GlobalValue::ExternalLinkage);
            F->setVisibility(GlobalValue::HiddenVisibility);
            F->setDSOLocal(true);
        }
    /* Locals only the moved bodies used are dead now; drop them so the
     * residual's own cache key does not change with the units' content. */
    for (bool again = true; again;) {
        again = false;
        for (auto it = M.global_begin(); it != M.global_end();) {
            GlobalVariable &G = *it++;
            G.removeDeadConstantUsers();
            if (G.hasLocalLinkage() && G.use_empty()) {
                G.eraseFromParent();
                again = true;
            }
        }
        for (auto it = M.begin(); it != M.end();) {
            Function &F = *it++;
            F.removeDeadConstantUsers();
            if (F.hasLocalLinkage() && F.use_empty()) {
                F.eraseFromParent();
                again = true;
            }
        }
    }
    for (Function &F : M) out->residual_insns += insn_count(F);
    out->ms_split = now_ms() - t0;

    double t1 = now_ms();
    auto *tm = reinterpret_cast<TargetMachine *>(tm_ref);
    job.target = &tm->getTarget();
    job.triple = tm->getTargetTriple();
    job.cpu = tm->getTargetCPU().str();
    job.features = tm->getTargetFeatureString().str();
    job.options = tm->Options;
    job.reloc = tm->getRelocationModel();
    job.code_model = tm->getCodeModel();
    job.opt_level = tm->getOptLevel();
    job.tm_ref = tm_ref;
    job.opts = o;
    int n = (int)job.raw.size();
    job.refs.resize((size_t)n);
    for (int i = 0; i < n; i++)
        job.paths.push_back(std::string(o->obj_base) + ".u" + std::to_string(i) + ".o");
    for (int i = 0; i < n; i++) job.order.push_back(i);
    std::stable_sort(job.order.begin(), job.order.end(), [&](int a, int b) {
        return pl.units[unit_of_job[a]].insns > pl.units[unit_of_job[b]].insns;
    });
    int threads = o->threads < 1 ? 1 : (o->threads > n ? n : o->threads);
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; t++) pool.emplace_back([&job] { job.worker(); });
    job.worker();
    for (auto &th : pool) th.join();
    out->ms_compile = now_ms() - t1;
    out->units_hit = job.hits;
    out->units_miss = job.misses;
    if (!job.error.empty()) {
        for (auto &p : job.paths) sys::fs::remove(p);
        *error = strdup(job.error.c_str());
        return -1;
    }

    /* Keep what the optimized units still use alive in the residual, so
     * post-optimization checks (extern library pruning, the scheduler-init
     * strip) see the whole program's references. */
    std::set<std::string> names;
    for (auto &v : job.refs) names.insert(v.begin(), v.end());
    SmallVector<GlobalValue *, 16> already;
    collectUsedGlobalVariables(M, already, false);
    collectUsedGlobalVariables(M, already, true);
    std::set<const GlobalValue *> listed(already.begin(), already.end());
    std::vector<GlobalValue *> keep;
    for (const std::string &nm : names) {
        GlobalValue *GV = M.getNamedValue(nm);
        if (!GV || nm.rfind("llvm.", 0) == 0 || listed.count(GV)) continue;
        if (auto *F = dyn_cast<Function>(GV); F && pl.fn_unit.count(F)) continue;
        keep.push_back(GV);
    }
    if (!keep.empty()) appendToCompilerUsed(M, keep);

    out->count = n;
    out->objs = static_cast<char **>(calloc((size_t)n, sizeof(char *)));
    for (int i = 0; i < n; i++) out->objs[i] = strdup(job.paths[i].c_str());
    return 1;
}
