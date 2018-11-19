#ifndef _DG_LLVM_VALUE_RELATION_ANALYSIS_H_
#define _DG_LLVM_VALUE_RELATION_ANALYSIS_H_

#include <list>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/CFG.h>

#include "dg/analysis/ValueRelations/ValueRelations.h"

#include "Relations.h"
#include "EqualityMap.h"
#include "ReadsMap.h"

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace dg {
namespace analysis {

class VROp {
protected:
    enum class VROpType { INSTRUCTION, ASSUME, NOOP } _type;
    VROp(VROpType t) : _type(t) {}

public:
    bool isInstruction() const { return _type == VROpType::INSTRUCTION; }
    bool isAssume() const { return _type == VROpType::ASSUME; }
    bool isNoop() const { return _type == VROpType::NOOP; }

#ifndef NDEBUG
    virtual void dump() const = 0;
#endif
};

struct VRNoop : public VROp {
    VRNoop() : VROp(VROpType::NOOP) {}

#ifndef NDEBUG
    void dump() const override {
        std::cout << "(noop)";
    }
#endif
};

struct VRInstruction : public VROp {
    const llvm::Instruction *instruction;
    VRInstruction(const llvm::Instruction *I)
    : VROp(VROpType::INSTRUCTION), instruction(I) {}

    const llvm::Instruction* getInstruction() const { return instruction; }

    static VRInstruction *get(VROp *op) {
        return op->isInstruction() ? static_cast<VRInstruction *>(op) : nullptr;
    }

#ifndef NDEBUG
    void dump() const override {
        std::cout << debug::getValName(instruction);
    }
#endif
};

struct VRAssume : public VROp {
    RelationsMap relations{};

    VRAssume() : VROp(VROpType::ASSUME) {}

    VRAssume(const VRRelation& rel)
    : VROp(VROpType::ASSUME) {
        relations.add(rel);
    }

    void addRelation(const VRRelation& rel) { relations.add(rel); }
    const RelationsMap& getRelations() const { return relations; }

    static VRAssume *get(VROp *op) {
        return op->isAssume() ? static_cast<VRAssume *>(op) : nullptr;
    }

#ifndef NDEBUG
    void dump() const override {
        std::cout << "[";
        relations.dump();
        std::cout << "]";
    }
#endif
};

struct VRLocation;
struct VREdge {
    VRLocation *source;
    VRLocation *target;

    std::unique_ptr<VROp> op;

    VREdge(VRLocation *s, VRLocation *t, std::unique_ptr<VROp>&& op)
    : source(s), target(t), op(std::move(op)) {}
};

struct VRLocation  {
    const unsigned id;

    // Valid equalities at this location
    EqualityMap<const llvm::Value *> equalities;
    // pairs (a,b) such that if we meet "load a", we know
    // the result is b
    ReadsMap reads;
    RelationsMap relations;

    std::vector<VREdge *> predecessors{};
    std::vector<std::unique_ptr<VREdge>> successors{};

    void addEdge(std::unique_ptr<VREdge>&& edge) {
        edge->target->predecessors.push_back(edge.get());
        successors.emplace_back(std::move(edge));
    }

    void transitivelyClose() {
        // add all equalities into relations
        for (auto& it : equalities) {
            for (auto& it2 : *(it.second.get()))
                relations.add(VRRelation::Eq(it.first, it2));
        }

        relations.transitivelyClose();
    }

    VRLocation(unsigned _id) : id(_id) {}

#ifndef NDEBUG
    void dump() const {
        std::cout << id << " ";
        std::cout << std::endl;
    }
#endif
};

struct VRBBlock {
    std::list<std::unique_ptr<VRLocation>> locations;

    void prepend(std::unique_ptr<VRLocation>&& loc) {
        locations.push_front(std::move(loc));
    }

    void append(std::unique_ptr<VRLocation>&& loc) {
        locations.push_back(std::move(loc));
    }

    VRLocation *last() { return locations.back().get(); }
    VRLocation *first() { return locations.front().get(); }
    const VRLocation *last() const { return locations.back().get(); }
    const VRLocation *first() const { return locations.front().get(); }
};

class LLVMValueRelationsAnalysis {
    // reads about which we know that always hold
    // (e.g. if the underlying memory is defined only at one place
    // or for global constants)
    std::set<const llvm::Value *> fixedMemory;
    const llvm::Module *_M;

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

    void initializeFixedReads() {
        using namespace llvm;

        // FIXME: globals
        for (auto &F : *_M) {
            for (auto& B : F) {
                for (auto& I : B) {
                    if (isOnceDefinedAlloca(&I)) {
                        fixedMemory.insert(&I);
                    }
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
                EqualityMap<const llvm::Value*>&,
                RelationsMap& Rel) {
        using namespace llvm;
        auto val1 = I->getOperand(0);
        auto val2 = I->getOperand(1);

        auto C1 = dyn_cast<ConstantInt>(val1);
        auto C2 = dyn_cast<ConstantInt>(val2);

        if ((!C1 && !C2) || (C1 && C2) /* FIXME! */)
            return false;
        if (C1 && !C2) {
            auto tmp = C1;
            C1 = C2;
            C2 = tmp;
            auto tmp1 = val1;
            val1 = val2;
            val2 = tmp1;
        }

        assert(!C1 && C2);

        auto V = C2->getSExtValue();
        if (V > 0)
            return Rel.add(VRRelation::Gt(I, val1));
        else if (V == 0)
            return Rel.add(VRRelation::Eq(I, val1));
        else
            return Rel.add(VRRelation::Lt(I, val1));

        abort();
    }

    // FIXME: do not duplicate the code
    bool minusGen(const llvm::Instruction *I,
                  EqualityMap<const llvm::Value*>&,
                  RelationsMap& Rel) {
        using namespace llvm;
        auto val1 = I->getOperand(0);
        auto val2 = I->getOperand(1);

        auto C1 = dyn_cast<ConstantInt>(val1);
        auto C2 = dyn_cast<ConstantInt>(val2);

        if ((!C1 && !C2) || (C1 && C2) /* FIXME! */)
            return false;
        if (C1 && !C2) {
            auto tmp = C1;
            C1 = C2;
            C2 = tmp;
            auto tmp1 = val1;
            val1 = val2;
            val2 = tmp1;
        }

        assert(!C1 && C2);

        auto V = C2->getSExtValue();
        if (V > 0)
            return Rel.add(VRRelation::Lt(I, val1));
        else if (V == 0)
            return Rel.add(VRRelation::Eq(I, val1));
        else
            return Rel.add(VRRelation::Gt(I, val1));

        abort();
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
                return plusGen(I, E, Rel);
            case Instruction::Sub:
                return minusGen(I, E, Rel);
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
            if (isa<AllocaInst>(writtenMem) || hasAlias(writtenMem, E)) {
                overwritesReads.insert(writtenMem);
                // overwrite aliases
                if (auto equiv = source->equalities.get(writtenMem)) {
                    overwritesReads.insert(equiv->begin(), equiv->end());
                }
                // overwrite also reads from memory that has no
                // aliases to an alloca inst
                // (we do not know whether it may be alias or not)
                for (auto& r : source->reads) {
                    if (!hasAlias(r.first, E)) {
                        overwritesReads.insert(r.first);
                    }
                }
            } else {
                overwritesAll = true;
            }
        } else if (I->mayWriteToMemory() || I->mayHaveSideEffects()) {
            overwritesAll = true;
        }
    }

    bool assumeGen(VRAssume *assume,
				   RelationsMap& Rel,
                   EqualityMap<const llvm::Value*>& E,
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

    bool mergePredecessors(VRLocation *loc) {
		assert(loc->predecessors.size() > 1);

        using namespace llvm;

        // merge equalities and relations that use only
        // fixed memory as these cannot change in the future
        // (constants, one-time-defined alloca's, and so on).
        // The rest would be too much time-consuming.
        bool changed = false;
        for (auto pred : loc->predecessors) {
            for (auto& it : pred->source->reads) {
                if (fixedMemory.count(it.first) > 0) {
                    auto LI = dyn_cast<LoadInst>(it.second);
                    if (LI && !fixedMemory.count(LI->getOperand(0)))
                        continue;
                    changed |= loc->reads.add(it.first, it.second);
                }
            }

            for (auto& it : pred->source->equalities) {
                auto LI = dyn_cast<LoadInst>(it.first);
                // XXX: we can do the same with constants
                if (LI && fixedMemory.count(LI->getOperand(0)) > 0) {
                    for (auto eq : *(it.second.get())) {
                        auto LI2 = dyn_cast<LoadInst>(eq);
                        if (LI2 && !fixedMemory.count(LI2->getOperand(0)))
                            continue;
                        changed |= loc->equalities.add(it.first, eq);
                        // add the first equality also into reads map,
                        // so that we can pair the values with further reads
                        if (auto rr = loc->reads.get(LI->getOperand(0))) {
                            loc->equalities.add(rr, eq);
                        } else {
                            loc->reads.add(LI->getOperand(0), eq);
                        }
                    }
                }
            }

            for (auto& it : pred->source->relations) {
                auto LI = dyn_cast<LoadInst>(it.first);
                // XXX: we can do the same with constants
                if (LI && fixedMemory.count(LI->getOperand(0)) > 0) {
                    for (const auto& R : it.second) {
                        assert(R.getLHS() == it.first);
                        auto LI2 = dyn_cast<LoadInst>(R.getRHS());
                        if (LI2 && !fixedMemory.count(LI2->getOperand(0)))
                            continue;
                        changed |= loc->relations.add(R);
                    }
                }
            }
        }

        return changed;
    }

public:
    template <typename Blocks>
    void run(Blocks& blocks) {
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
    }

    LLVMValueRelationsAnalysis(const llvm::Module *M) : _M(M) {
        initializeFixedReads();
    }

};

class LLVMValueRelations {
    const llvm::Module *_M;

    unsigned last_node_id{0};
    // mapping from LLVM Values to relevant CFG nodes
    std::map<const llvm::Value *, VRLocation *> _loc_mapping;
    // list of our basic blocks with mapping from LLVM
    std::map<const llvm::BasicBlock *, std::unique_ptr<VRBBlock>> _blocks;

    VRLocation *newLocation(const llvm::Value *v = nullptr) {
        auto loc = new VRLocation(++last_node_id);
        if (v)
            _loc_mapping[v] = loc;
        return loc;
    }

    VRBBlock *newBBlock(const llvm::BasicBlock *B) {
        assert(_blocks.find(B) == _blocks.end());
        auto block = new VRBBlock();
        _blocks.emplace(std::piecewise_construct,
                        std::forward_as_tuple(B),
                        std::forward_as_tuple(block));
        return block;
    }

    VRBBlock *getBBlock(const llvm::BasicBlock *B) {
        auto it = _blocks.find(B);
        return it == _blocks.end() ? nullptr : it->second.get();
    }

    void build(const llvm::BasicBlock& B) {
        auto block = newBBlock(&B);
        const llvm::Instruction *lastInst{nullptr};

        for (const auto& I : B) {
            auto loc = std::unique_ptr<VRLocation>(newLocation(&I));

            if (lastInst) {
                auto edge
                    = std::unique_ptr<VREdge>(
                            new VREdge(block->last(), loc.get(),
                                       std::unique_ptr<VROp>(new VRInstruction(lastInst))));

                block->last()->addEdge(std::move(edge));
            }

            block->append(std::move(loc));
            lastInst = &I;
        }
    }

    void buildSwitch(const llvm::SwitchInst *swtch, VRBBlock *block) {
        for (auto& it : swtch->cases()) {
            auto succ = getBBlock(it.getCaseSuccessor());
            assert(succ);
            auto op = std::unique_ptr<VROp>(new VRAssume(
                VRRelation::Eq(swtch->getCondition(), it.getCaseValue())));

            block->last()->addEdge(std::unique_ptr<VREdge>(
                                    new VREdge(block->last(),
                                               succ->first(),
                                               std::move(op))));
        }

        // add 'default' branch
        auto succ = getBBlock(swtch->getDefaultDest());
        assert(succ);
        auto assume = new VRAssume();

        for (auto& it : swtch->cases()) {
            assume->addRelation(VRRelation::Neq(swtch->getCondition(),
                                                it.getCaseValue()));
        }
        // we could gather at least some conditions.
        auto op = std::unique_ptr<VROp>(assume);
        block->last()->addEdge(std::unique_ptr<VREdge>(
                                new VREdge(block->last(),
                                           succ->first(),
                                           std::move(op))));
    }

    VRRelation getICmpRelation(const llvm::Value *val) const {
		using namespace llvm;
		VRRelation rel;
        auto CMP = dyn_cast<ICmpInst>(val);
        if (!CMP)
            return rel;

        auto val1 = CMP->getOperand(0);
        auto val2 = CMP->getOperand(1);

        switch (CMP->getSignedPredicate()) {
            case ICmpInst::Predicate::ICMP_EQ:
                rel = VRRelation::Eq(val1, val2); break;
            case ICmpInst::Predicate::ICMP_NE:
                rel = VRRelation::Neq(val1, val2); break;
            case ICmpInst::Predicate::ICMP_ULE:
            case ICmpInst::Predicate::ICMP_SLE:
                rel = VRRelation::Le(val1, val2); break;
            case ICmpInst::Predicate::ICMP_UGE:
            case ICmpInst::Predicate::ICMP_SGE:
                rel = VRRelation::Ge(val1, val2); break;
            case ICmpInst::Predicate::ICMP_UGT:
            case ICmpInst::Predicate::ICMP_SGT:
                rel = VRRelation::Gt(val1, val2); break;
            case ICmpInst::Predicate::ICMP_ULT:
            case ICmpInst::Predicate::ICMP_SLT:
                rel = VRRelation::Lt(val1, val2); break;
            default:
#ifndef NDEBUG
                errs() << "Unhandled predicate in" << *CMP << "\n";
#endif
                abort();
		}

        return rel;
    }

    void buildBranch(const llvm::BranchInst *br, VRBBlock *block) {
        if (br->isConditional()) {
            auto trueSucc = getBBlock(br->getSuccessor(0));
            auto falseSucc = getBBlock(br->getSuccessor(1));
            assert(trueSucc && falseSucc);
            std::unique_ptr<VROp> trueOp{}, falseOp{};

            auto relation = getICmpRelation(br->getCondition());
            if (relation) {
                trueOp.reset(new VRAssume(relation));
                falseOp.reset(new VRAssume(VRRelation::Not(relation)));
            } else {
                trueOp.reset(new VRNoop());
                falseOp.reset(new VRNoop());
            }

            auto trueEdge = std::unique_ptr<VREdge>(new VREdge(block->last(),
                                                               trueSucc->first(),
                                                               std::move(trueOp)));
            auto falseEdge = std::unique_ptr<VREdge>(new VREdge(block->last(),
                                                               falseSucc->first(),
                                                               std::move(falseOp)));
            block->last()->addEdge(std::move(trueEdge));
            block->last()->addEdge(std::move(falseEdge));
        } else {
            auto succ = getBBlock(br->getSuccessor(0));
            assert(succ);
            auto op = std::unique_ptr<VROp>(new VRNoop());
            block->last()->addEdge(std::unique_ptr<VREdge>(
                                    new VREdge(block->last(),
                                               succ->first(),
                                               std::move(op))));
        }
    }

    void build(const llvm::Function& F) {
        for (const auto& B : F) {
            assert(B.size() != 0);
            build(B);
        }

        for (const auto& B : F) {
            assert(B.size() != 0);
            auto block = getBBlock(&B);
            assert(block);

            // add generated constrains
            auto term = B.getTerminator();
            if (llvm::isa<llvm::BranchInst>(term)) {
                buildBranch(llvm::cast<llvm::BranchInst>(term), block);
            } else if (llvm::isa<llvm::SwitchInst>(term)) {
                buildSwitch(llvm::cast<llvm::SwitchInst>(term), block);
            } else if (llvm::succ_begin(&B) != llvm::succ_end(&B)) {
#ifndef NDEBUG
                llvm::errs() << "Unhandled terminator: " << *term << "\n";
#endif
                abort();
            }
        }
    }

public:
    LLVMValueRelations(const llvm::Module *M) : _M(M) {}

    VRLocation *getMapping(const llvm::Value *v) {
        auto it = _loc_mapping.find(v);
        return it == _loc_mapping.end() ? nullptr : it->second;
    }

    const VRLocation *getMapping(const llvm::Value *v) const {
        auto it = _loc_mapping.find(v);
        return it == _loc_mapping.end() ? nullptr : it->second;
    }


    void build() {
        for (const auto& F : *_M) {
            build(F);
        }
    }

    // FIXME: this should be for each node
    void compute() {
        LLVMValueRelationsAnalysis VRA(_M);
        VRA.run(_blocks);
    }

    decltype(_blocks) const& getBlocks() const {
        return _blocks;
    }

    bool isLt(const llvm::Value *where, const llvm::Value *a, const llvm::Value *b) {
        auto A = getMapping(where);
        assert(A);
        // FIXME: this is really not efficient
        A->transitivelyClose();
        auto aRel = A->relations.get(a);
        return aRel ? aRel->has(VRRelationType::LT, b) : false;
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_VALUE_RELATION_ANALYSIS_H_
