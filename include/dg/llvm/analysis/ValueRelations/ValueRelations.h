#ifndef _DG_LLVM_VALUE_RELATION_ANALYSIS_H_
#define _DG_LLVM_VALUE_RELATION_ANALYSIS_H_

#include <list>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instructions.h>
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
    const llvm::Value *value;
    bool istrue;

    VRAssume(const llvm::Value *V, bool istrue)
    : VROp(VROpType::ASSUME), value(V), istrue(istrue) {}

    bool isTrue() const { return istrue; }
    bool isFalse() const { return !istrue; }
    const llvm::Value *getValue() const { return value; }

    static VRAssume *get(VROp *op) {
        return op->isAssume() ? static_cast<VRAssume *>(op) : nullptr;
    }

#ifndef NDEBUG
    void dump() const override {
        if (isFalse())
            std::cout << "!";
        std::cout << "[";
        std::cout << debug::getValName(value);
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

    bool isOnceDefinedAlloca(const llvm::Instruction *I) {
        using namespace llvm;
        if (auto AI = dyn_cast<AllocaInst>(I)) {
            bool had_store = false;
            for (auto it = AI->use_begin(), et = AI->use_end(); it != et; ++it) {
            #if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
                const Value *use = *it;
            #else
                const Value *use = it->getUser();
            #endif

                // we must have maximally one store
                // and the rest of instructions must be loads
                // (this is maybe too strict, but...
                if (auto SI = dyn_cast<StoreInst>(use)) {
                    if (had_store)
                        return false;
                    had_store = true;
                    // the address is taken, it can be used via a pointer
                    if (SI->getOperand(0) == AI)
                        return false;
                } else if (!isa<LoadInst>(use)) {
                    return false;
                }
            }

            return true;
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

    bool instructionGen(const llvm::Instruction *I,
                        EqualityMap<const llvm::Value*>& E,
                        RelationsMap& Rel,
                        ReadsMap& R, VRLocation *source) {
        using namespace llvm;
        if (auto SI = dyn_cast<StoreInst>(I)) {
            auto writtenMem = SI->getOperand(1)->stripPointerCasts();
            return R.add(writtenMem, SI->getOperand(0));
        } else if (auto LI = dyn_cast<LoadInst>(I)) {
            return loadGen(LI, E, Rel, R, source);
        } else if (auto GEP = dyn_cast<GetElementPtrInst>(I)) {
            return gepGen(GEP, E, R, source);
        } else if (auto C = dyn_cast<CastInst>(I)) {
            if (C->isLosslessCast() || isa<ZExtInst>(C) || // (S)ZExt should not change value
                isa<SExtInst>(C) || C->isNoopCast(_M->getDataLayout())) {
                return E.add(C, C->getOperand(0));
            }
        }
        return false;
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
		using namespace llvm;
        auto CMP = dyn_cast<ICmpInst>(assume->getValue());
        if (!CMP)
            return false;

        auto val1 = CMP->getOperand(0);
        auto val2 = CMP->getOperand(1);
		bool changed = false;
		VRRelation rel;

        switch (CMP->getSignedPredicate()) {
            case ICmpInst::Predicate::ICMP_EQ:
				if (assume->isTrue())
                    changed |= E.add(val1, val2);
                rel = VRRelation::Eq(val1, val2); break;
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
            default: abort();
		}

		changed |= Rel.add(assume->isTrue() ? rel : VRRelation::Not(rel));
        return changed;
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
                    bool first = true;
                    for (auto eq : *(it.second.get())) {
                        auto LI2 = dyn_cast<LoadInst>(eq);
                        if (LI2 && !fixedMemory.count(LI2->getOperand(0)))
                            continue;
                        changed |= loc->equalities.add(it.first, eq);
                        // add the first equality also into reads map,
                        // so that we can pair the values with
                        // further reads
                        if (first) {
                            if (!loc->reads.get(it.first)) {
                                loc->reads.add(LI->getOperand(0), eq);
                                first = false;
                            }
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
            auto br = llvm::dyn_cast<llvm::BranchInst>(term);
            if (!br) {
                if (llvm::succ_begin(&B) != llvm::succ_end(&B)) {
#ifndef NDEBUG
                    llvm::errs() << "Unhandled terminator: " << *term << "\n";
#endif
                    abort();
                }
                continue; // no successor
            }

            if (br->isConditional()) {
                auto trueSucc = getBBlock(br->getSuccessor(0));
                auto falseSucc = getBBlock(br->getSuccessor(1));
                assert(trueSucc && falseSucc);
                auto trueOp
                    = std::unique_ptr<VROp>(new VRAssume(br->getCondition(), true));
                auto falseOp
                    = std::unique_ptr<VROp>(new VRAssume(br->getCondition(), false));

                auto trueEdge = std::unique_ptr<VREdge>(new VREdge(block->last(),
                                                                   trueSucc->first(),
                                                                   std::move(trueOp)));
                auto falseEdge = std::unique_ptr<VREdge>(new VREdge(block->last(),
                                                                   falseSucc->first(),
                                                                   std::move(falseOp)));
                block->last()->addEdge(std::move(trueEdge));
                block->last()->addEdge(std::move(falseEdge));
                continue;
            } else {
                auto llvmsucc = B.getSingleSuccessor();
                assert(llvmsucc);
                auto succ = getBBlock(llvmsucc);
                assert(succ);
                auto op = std::unique_ptr<VROp>(new VRNoop());
                block->last()->addEdge(std::unique_ptr<VREdge>(
                                        new VREdge(block->last(),
                                                   succ->first(),
                                                   std::move(op))));
            }
        }
    }

public:
    LLVMValueRelations(const llvm::Module *M) : _M(M) {}

    VRLocation *getMapping(const llvm::Value *v) {
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
        A->relations.transitivelyClose();
        auto aRel = A->relations.get(a);
        return aRel ? aRel->has(VRRelationType::LT, b) : false;
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_VALUE_RELATION_ANALYSIS_H_
