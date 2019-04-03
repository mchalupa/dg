#ifndef _DG_LLVM_VALUE_RELATION_ANALYSIS_H_
#define _DG_LLVM_VALUE_RELATION_ANALYSIS_H_

#include <list>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Value.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/CFG.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/analysis/ValueRelations/ValueRelations.h"

#include "Graph.h"
#include "Relations.h"
#include "EqualityMap.h"
#include "ReadsMap.h"

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace dg {
namespace analysis {

static inline std::pair<const llvm::Value *, int64_t>
getOperationWithConst(const llvm::Instruction *I) {
    using namespace llvm;
    auto val1 = I->getOperand(0);
    auto val2 = I->getOperand(1);

    auto C1 = dyn_cast<ConstantInt>(val1);
    auto C2 = dyn_cast<ConstantInt>(val2);

    if ((!C1 && !C2) || (C1 && C2) /* FIXME! */)
        return {nullptr, 0};
    if (C1 && !C2) {
        auto tmp = C1;
        C1 = C2;
        C2 = tmp;
    }

    assert(!C1 && C2);
    return {C1, C2->getSExtValue()};
}

namespace {
// adjusted from LLVM sources
static inline
const llvm::BasicBlock *getBasicBlockUniqueSuccessor(const llvm::BasicBlock *B) {
#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
    auto SI = succ_begin(B), E = succ_end(B);
    if (SI == E)
        return nullptr; // No successors
    auto SuccBB = *SI;
    ++SI;
    for (;SI != E; ++SI) {
        if (*SI != SuccBB)
            return nullptr;
    }

    return SuccBB;
#else
    return B->getUniqueSuccessor();
#endif // LLVM 3.6
}
}

class LLVMValueRelationsAnalysis {
    // reads about which we know that always hold
    // (e.g. if the underlying memory is defined only at one place
    // or for global constants)
    std::set<const llvm::Value *> fixedMemory;
    // values that might never change
    std::set<const llvm::Value *> fixedValues;
    const llvm::Module *_M;
    unsigned _max_iterations = 0;

    size_t mayBeWritten(const llvm::Value *v) const {
        using namespace llvm;
        for (auto it = v->use_begin(), et = v->use_end(); it != et; ++it) {
        #if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
            const Value *use = *it;
        #else
            const Value *use = it->getUser();
        #endif

            // we may write to this memory or store the pointer
            // somewhere and therefore write later through it to memory
            if (isa<StoreInst>(use)) {
                return true;
            } else if (auto CI = dyn_cast<CastInst>(use)) {
                if (mayBeWritten(CI))
                    return true;
            } else if (!isa<LoadInst>(use) &&
                       !isa<DbgDeclareInst>(use) &&
                       !isa<DbgValueInst>(use)) { // Load and dbg are ok
                if (auto II = dyn_cast<IntrinsicInst>(use)) {
                    switch(II->getIntrinsicID()) {
                        case Intrinsic::lifetime_start:
                        case Intrinsic::lifetime_end:
                        case Intrinsic::stacksave:
                        case Intrinsic::stackrestore:
                        case Intrinsic::dbg_declare:
                        case Intrinsic::dbg_value:
                            continue;
                        default:
                            if (II->mayWriteToMemory())
                            return true;
                    }
                }
                return true;
            }
        }

        return false;
    }

    size_t writtenMaxOnce(const llvm::Value *v) const {
        using namespace llvm;
        bool had_store = false;
        for (auto it = v->use_begin(), et = v->use_end(); it != et; ++it) {
        #if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
            const Value *use = *it;
        #else
            const Value *use = it->getUser();
        #endif

            if (auto SI = dyn_cast<StoreInst>(use)) {
                if (SI->getPointerOperand()->stripPointerCasts() == v) {
                    if (had_store) {
                        return false;
                    }
                    had_store = true;
                }
            } else if (auto CI = dyn_cast<CastInst>(use)) {
                if (mayBeWritten(CI)) {
                    return false;
                }
            } else if (auto I = dyn_cast<Instruction>(use)) {
                if (I->mayWriteToMemory()) {
                    return false;
                }
            }
        }

        return true;
    }

    bool cannotEscape(const llvm::Value *v) const {
        using namespace llvm;

        if (!v->getType()->isPointerTy())
            return true;

        for (auto it = v->use_begin(), et = v->use_end(); it != et; ++it) {
        #if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
            const Value *use = *it;
        #else
            const Value *use = it->getUser();
        #endif
            // we must only store into it, not store this
            // value somewhere
            if (auto SI = dyn_cast<StoreInst>(use)) {
                if (SI->getOperand(0) == v) {
                    return false;
                }
            } else if (auto CI = dyn_cast<CastInst>(use)) {
                if (!cannotEscape(CI)) {
                    return false;
                }
            // otherwise, we can only load from this value
            // or use it in debugging informations
            } else if (!isa<LoadInst>(use) &&
                       !isa<DbgDeclareInst>(use) &&
                       !isa<DbgValueInst>(use)) {
                if (auto II = dyn_cast<IntrinsicInst>(use)) {
                    switch(II->getIntrinsicID()) {
                        case Intrinsic::lifetime_start:
                        case Intrinsic::lifetime_end:
                        case Intrinsic::stacksave:
                        case Intrinsic::stackrestore:
                        case Intrinsic::dbg_declare:
                        case Intrinsic::dbg_value:
                            continue;
                        default:
                            if (!II->mayWriteToMemory())
                                continue;
                            return false;
                    }
                }
                return false;
            }
        }

        return true;
    }

    bool isOnceDefinedAlloca(const llvm::Instruction *I) {
        using namespace llvm;
        if (isa<AllocaInst>(I)) {
            return cannotEscape(I) && writtenMaxOnce(I);
        }

        return false;
    }

    void initializeFixed() {
        using namespace llvm;

        // FIXME: globals
        for (auto &F : *_M) {
            for (auto& B : F) {
                for (auto& I : B) {
                    if (isOnceDefinedAlloca(&I)) {
                        //llvm::errs() << "Fixed memory: " << I << "\n";
                        fixedMemory.insert(&I);
                    }
                }
            }

            // FIXME: this is correct only for non-recursive functions
            // TODO: we can do this search also after branching,
            // we just must stop at the first join on each path
            if (!F.isDeclaration()) {
                std::set<const BasicBlock *> visited;
                auto B = &F.getEntryBlock();
                while (B) {
                    if (!visited.insert(B).second)
                        break;
                    for (auto& I : *B)
                        fixedValues.insert(&I);
                    B = getBasicBlockUniqueSuccessor(B);
                }
            }
        }
    }

    static bool hasAlias(const llvm::Value *val,
                         EqualityMap<const llvm::Value *>& E) {
        auto equiv = E.get(val);
        if (!equiv)
            return false;
        for (auto alias : *equiv) {
            if (llvm::isa<llvm::AllocaInst>(alias)) {
                //llvm::errs() << *val << " has alias " << *alias << "\n";
                return true;
            }
        }
        return false;
    }

    bool loadGen(const llvm::LoadInst *LI,
                 EqualityMap<const llvm::Value*>& E,
                 RelationsMap&,
                 ReadsMap& R,
                 VRLocation *source) {
        auto readFrom = LI->getOperand(0);
        auto readVal = source->reads.get(readFrom);
        if (!readVal) {
            // try read from aliases, we may get lucky there
            // (as we do not add all equivalent reads to the map of reads)
            // XXX: make an alias iterator
            auto equiv = source->equalities.get(LI->getOperand(0));
            if (equiv) {
                for (auto alias : *equiv) {
                    if ((readVal = source->reads.get(alias))) {
                        break;
                    }
                }
            }
            // it is not a load from known value,
            // so remember that the loaded value was read
            // by this load -- in the future, we may be able
            // to pair it with another same laod
            if (!readVal) {
                return R.add(LI->getOperand(0), LI);
            }
        }

        return E.add(LI, readVal);
    }

    bool gepGen(const llvm::GetElementPtrInst *GEP,
                EqualityMap<const llvm::Value*>& E,
                ReadsMap&,
                VRLocation *) {

        if (GEP->hasAllZeroIndices()) {
            return E.add(GEP, GEP->getPointerOperand());
        }

        // we can also add < > according to shift of offset

        return false;
    }

    bool plusGen(const llvm::Instruction *I,
                 // known equalities
                 EqualityMap<const llvm::Value*>& curE,
                 // new equalities
                 EqualityMap<const llvm::Value*>& E,
                RelationsMap& Rel) {
        using namespace llvm;
        const Value *val1;
        int64_t V;
        std::tie(val1, V) = getOperationWithConst(I);
        if (!val1)
            return false;

        // check casses (x - k) + k
        if (auto equiv = curE.get(val1)) {
            for (auto eq : *equiv) {
                if (auto BI = dyn_cast<BinaryOperator>(eq)) {
                    if (BI->getOpcode() == Instruction::Sub) {
                        auto vals = getOperationWithConst(BI);
                        if (vals.first && vals.second == V)
                            E.add(vals.first, I);
                    }
                }
            }
        }

        if (V > 0)
            return Rel.add(VRRelation::Gt(I, val1));
        else if (V == 0)
            return Rel.add(VRRelation::Eq(I, val1));
        else
            return Rel.add(VRRelation::Lt(I, val1));

        abort();
    }

    bool minusGen(const llvm::Instruction *I,
                  // known equalities
                  EqualityMap<const llvm::Value*>& curE,
                  // new equalities
                  EqualityMap<const llvm::Value*>& E,
                  RelationsMap& Rel) {
        using namespace llvm;
        const Value *val1;
        int64_t V;
        std::tie(val1, V) = getOperationWithConst(I);
        if (!val1)
            return false;

        // check casses (x + k) - k
        if (auto equiv = curE.get(val1)) {
            for (auto eq : *equiv) {
                if (auto BI = dyn_cast<BinaryOperator>(eq)) {
                    if (BI->getOpcode() == Instruction::Add) {
                        auto vals = getOperationWithConst(BI);
                        if (vals.first && vals.second == V)
                            E.add(vals.first, I);
                    }
                }
            }
        }

        if (V > 0)
            return Rel.add(VRRelation::Lt(I, val1));
        else if (V == 0)
            return Rel.add(VRRelation::Eq(I, val1));
        else
            return Rel.add(VRRelation::Gt(I, val1));

        abort();
    }

    bool mulGen(const llvm::Instruction *I,
                EqualityMap<const llvm::Value*>& E,
                RelationsMap&) {
        const llvm::Value *val1;
        int64_t V;
        std::tie(val1, V) = getOperationWithConst(I);
        if (!val1)
            return false;

        if (V == 1)
            return E.add(I, val1);
        return false;
    }


    bool instructionGen(const llvm::Instruction *I,
                        EqualityMap<const llvm::Value*>& E,
                        RelationsMap& Rel,
                        ReadsMap& R, VRLocation *source) {
        using namespace llvm;
        switch(I->getOpcode()) {
            case Instruction::Store:
                return R.add(I->getOperand(1)->stripPointerCasts(), I->getOperand(0));
            case Instruction::Load:
                return loadGen(cast<LoadInst>(I), E, Rel, R, source);
            case Instruction::GetElementPtr:
                return gepGen(cast<GetElementPtrInst>(I), E, R, source);
            case Instruction::ZExt:
            case Instruction::SExt: // (S)ZExt should not change value
                return E.add(I, I->getOperand(0));
            case Instruction::Add:
                return plusGen(I, source->equalities, E, Rel);
            case Instruction::Sub:
                return minusGen(I, source->equalities, E, Rel);
            case Instruction::Mul:
                return mulGen(I, E, Rel);
            default:
                if (auto C = dyn_cast<CastInst>(I)) {
                    if (C->isLosslessCast() || C->isNoopCast(_M->getDataLayout())) {
                        return E.add(C, C->getOperand(0));
                    }
                }
                return false;
        }
    }

    void instructionKills(const llvm::Instruction *I,
                        EqualityMap<const llvm::Value*>& E,
                        VRLocation *source,
                        std::set<const llvm::Value *>& overwritesReads,
                        bool& overwritesAll) {
        using namespace llvm;
        if (auto SI = dyn_cast<StoreInst>(I)) {
            auto writtenMem = SI->getOperand(1)->stripPointerCasts();
            // if we know into which memory we write through some offset,
            // we do not need to kill everything
            if (auto GEP = dyn_cast<GetElementPtrInst>(writtenMem)) {
                if (isa<AllocaInst>(GEP->getPointerOperand()) ||
                    hasAlias(GEP->getPointerOperand(), E))
                    writtenMem = GEP->getPointerOperand();
            }

            if (isa<AllocaInst>(writtenMem) || hasAlias(writtenMem, E)) {
                overwritesReads.insert(writtenMem);
                // overwrite aliases
                if (auto equiv = source->equalities.get(writtenMem)) {
                    overwritesReads.insert(equiv->begin(), equiv->end());
                }
                // overwrite also reads from memory that is not alloc
                // and has no aliases to an alloca inst (or that are not
                // GEP to an alloca).
                // (we do not know whether it may be alias or not)
                for (auto& r : source->reads) {
                    if (!isa<AllocaInst>(r.first) && !hasAlias(r.first, E)) {
                        if (auto GEP = dyn_cast<GetElementPtrInst>(r.first)) {
                            if (isa<AllocaInst>(GEP->getPointerOperand()) ||
                                hasAlias(GEP->getPointerOperand(), E))
                                // we know what this overwrites, we're fine
                                continue;
                        }
                        overwritesReads.insert(r.first);
                    }
                }
            } else {
                overwritesAll = true;
            }
            return;
        } else if (isa<CallInst>(I)) {
            if (auto II = dyn_cast<IntrinsicInst>(I)) {
                switch(II->getIntrinsicID()) {
                    case Intrinsic::lifetime_start:
                    case Intrinsic::lifetime_end:
                    case Intrinsic::stacksave:
                    case Intrinsic::stackrestore:
                    case Intrinsic::dbg_declare:
                    case Intrinsic::dbg_value:
                        return;
                    default: break; // fall-through
                }
            }
            // fall-through
        }

        if (I->mayWriteToMemory() || I->mayHaveSideEffects()) {
            overwritesAll = true;
        }
    }

    bool assumeGen(VRAssume *assume,
				   RelationsMap& Rel,
                   EqualityMap<const llvm::Value*>&,
                   VRLocation *) {
        // XXX: should we add also equivalent relations? I guess not,
        // these are handled when searched...
		return Rel.add(assume->getRelations());
    }

    // collect information via an edge from a single predecessor
    // and store it in E and R
    bool collect(VRLocation *loc,
                 EqualityMap<const llvm::Value*>& E,
				 RelationsMap& Rel,
                 ReadsMap& R,
                 VREdge *edge) {
        auto source = edge->source;
        std::set<const llvm::Value *> overwritesReads;
        bool overwritesAll = false;
        bool changed = false;

        ///
        // -- gen
        if (edge->op->isAssume()) {
            auto assume = VRAssume::get(edge->op.get());
            changed |= assumeGen(assume, Rel, E, source);
            // FIXME, may be equality too
        } else if (edge->op->isInstruction()) {
            auto I = VRInstruction::get(edge->op.get())->getInstruction();
            changed |= instructionGen(I, E, Rel, R, source);

            instructionKills(I, E, source, overwritesReads, overwritesAll);
        }

        ///
        // -- merge && kill
        changed |= loc->equalities.add(source->equalities);
        changed |= loc->relations.add(source->relations);

        if (overwritesAll) { // no merge
            return changed;
        }

        for (auto& it : source->reads) {
            if (overwritesReads.count(it.first) > 0)
                continue;
            changed |= R.add(it.first, it.second);
        }

        return changed;
    }

    bool collect(VRLocation *loc, VREdge *edge) {
        return collect(loc, loc->equalities, loc->relations, loc->reads, edge);
    }

    // merge information from predecessors
    bool collect(VRLocation *loc) {
        if (loc->predecessors.size() > 1) {
            return mergePredecessors(loc);
        } else if (loc->predecessors.size() == 1 ){
            return collect(loc, *loc->predecessors.begin());
        }
        return false;
    }

    // the only values that might not be changed after join are
    // loads from fixed memory and constants and fixed-memory allocation
    // addresses
    bool mightBeChanged(const llvm::Value *v) {
        auto LI = llvm::dyn_cast<llvm::LoadInst>(v);
        if (LI) {
            return fixedMemory.count(LI->getOperand(0)) == 0;
        }

        if (llvm::isa<llvm::Argument>(v) || llvm::isa<llvm::Constant>(v))
            return false;
        if (auto CI = llvm::dyn_cast<llvm::CastInst>(v))
            return mightBeChanged(CI->getOperand(0));

        if (auto BI = llvm::dyn_cast<llvm::BinaryOperator>(v))
            return mightBeChanged(BI->getOperand(0)) || mightBeChanged(BI->getOperand(1));

        // is it alloca that is in fixedMemory?
        return fixedMemory.count(v) == 0 && fixedValues.count(v) == 0;
    }

    bool mergeReads(VRLocation *loc, VREdge *pred) {
        return mergeReads(loc, pred->source);
    }

    bool mergeReads(VRLocation *loc, VRLocation *source) {
        using namespace llvm;
        bool changed = false;
        for (auto& it : source->reads) {
            if (fixedMemory.count(it.first) > 0) {
                // if it is load but not from fixed memory,
                // we don't want it
                if (mightBeChanged(it.second))
                    continue;
                if (auto r = loc->reads.get(it.first)) {
                    // we already have read, so add just equality
                    changed |= loc->equalities.add(r, it.second);
                } else {
                    changed |= loc->reads.add(it.first, it.second);
                }
            }
        }
        return changed;
    }

    bool addLoadFromEq(VRLocation *loc,const llvm::Value *v1, const llvm::Value *v2) {
        auto LI = llvm::dyn_cast<llvm::LoadInst>(v1);
        if (!LI)
            return false;

        // I know that L(v1) == rr && L(v1) == v2, therefore rr == v2
        if (auto rr = loc->reads.get(LI->getOperand(0))) {
            return loc->equalities.add(rr, v2);
        } else {
            // just add it as a read
            return loc->reads.add(LI->getOperand(0), v2);
        }
    }

    bool mergeEqualities(VRLocation *loc, VREdge *pred) {
        return mergeEqualities(loc, pred->source);
    }

    bool mergeEqualities(VRLocation *loc, VRLocation *source) {
        using namespace llvm;
        bool changed = false;

        for (auto& it : source->equalities) {
            if (mightBeChanged(it.first))
                continue;
            for (auto eq : *(it.second.get())) {
                if (mightBeChanged(eq))
                    continue;

                changed |= loc->equalities.add(it.first, eq);
                // add the equality also into reads map if we do not
                // have any read yet, so that we can pair the values
                // with further reads
                changed |= addLoadFromEq(loc, it.first, eq);
                changed |= addLoadFromEq(loc, eq, it.first);
            }
        }

        return changed;
    }

    bool mergeRelations(VRLocation *, VREdge *) {
        using namespace llvm;
        bool changed = false;

        /* NOTE: This is not sound. If we have contradictory
         * relations, we would add just one of them.
         * This is not monotonic as newly discovered relations
         * can remove old relations and we must propagate it
         * into reachable nodes. Therefore, do not merge any relation.
         * Note that for equalities we don't care as there
         * the fixed memory cannot change and if we gather some
         * contradictory constraints, then it must be because
         * the code is unreachable, not because we took different branches
         * and then joined the assumptions.
        for (auto& it : pred->source->relations) {
            if (mightBeChanged(it.first))
                continue;
            for (const auto& R : it.second) {
                assert(R.getLHS() == it.first);
                if (mightBeChanged(R.getRHS()))
                    continue;
                if (loc->relations.has(VRRelation::Not(R)))
                    continue;
                changed |= loc->relations.add(R);
            }
        }
        */

        return changed;
    }

    bool mergePredecessors(VRLocation *loc) {
		assert(loc->predecessors.size() > 1);
        using namespace llvm;

        // merge equalities and relations that use only
        // fixed memory as these cannot change in the future
        // (constants, one-time-defined alloca's, and so on).
        // The rest would be too much time-consuming.
        bool changed = false;
        for (auto pred : loc->predecessors) {
            changed |= mergeReads(loc, pred);
            changed |= mergeEqualities(loc, pred);
            //changed |= mergeRelations(loc, pred);
        }
        return changed;
    }

public:
    template <typename Locs>
    bool mergeStates(VRLocation *dest, Locs& locs) {
        bool changed = false;
        for (auto loc : locs) {
            changed |= mergeReads(dest, loc);
            changed |= mergeEqualities(dest, loc);
        }
        return changed;
    }

    template <typename Blocks>
    bool run(Blocks& blocks) {
        // FIXME: only nodes reachable from changed nodes
        bool changed;
        unsigned n = 0;
        do {
            ++n;

#ifndef NDEBUG
            if (n % 1000 == 0) {
                llvm::errs() << "Iterations: " << n << "\n";
            }
#endif

            if (_max_iterations && (n > _max_iterations))
                break;

            changed = false;
            for (const auto& B : blocks) {
                for (const auto& loc : B.second->locations) {
                    changed |= collect(loc.get());
                }
            }
        } while (changed);

#ifndef NDEBUG
        llvm::errs() << "Number of iterations: " << n << "\n";
#endif
        return changed;
    }

    LLVMValueRelationsAnalysis(const llvm::Module *M,
                               unsigned max_iterations = 0)
    : _M(M), _max_iterations(max_iterations) {
        initializeFixed();
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_VALUE_RELATION_ANALYSIS_H_
