#ifndef _DG_LLVM_VALUE_RELATION_H_
#define _DG_LLVM_VALUE_RELATION_H_

#include <llvm/IR/Value.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/CFG.h>

#include "dg/analysis/ValueRelations/ValueRelations.h"

#include "Graph.h"
#include "Relations.h"
#include "EqualityMap.h"
#include "ReadsMap.h"
#include "ValueRelationsAnalysis.h"

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace dg {
namespace analysis {

class LLVMValueRelations {
    const llvm::Module *_M;
    unsigned _max_interprocedural_iterations = 0;

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

    void passCallSiteRelations(LLVMValueRelationsAnalysis& analysis) {
        using namespace llvm;
        for (auto& F : *_M) {
            if (F.isDeclaration())
                continue;
            F.getEntryBlock();
            auto& ourBlk = _blocks[&F.getEntryBlock()];
            assert(ourBlk);

            std::vector<VRLocation *> calls;
            for (auto it = F.use_begin(), et = F.use_end(); it != et; ++it) {
            #if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
                const Value *use = *it;
            #else
                const Value *use = it->getUser();
            #endif
                auto CI = dyn_cast<CallInst>(use);
                if (!CI)
                    continue;

                auto loc = getMapping(use);
                assert(loc);
                calls.push_back(loc);

                // bind arguments
                unsigned n = 0;
                for (auto& arg : F.args()) {
                    if (n >= CI->getNumArgOperands())
                        break;
                    auto op = CI->getArgOperand(n);
                    ourBlk->first()->equalities.add(&arg, op);

                    if (auto EQ = loc->equalities.get(op)) {
                        for (auto eq : *EQ)
                            ourBlk->first()->equalities.add(&arg, eq);
                    }
                    ++n;
                }
            }


            // take callers of this function and pass relation
            // from them to the entry instruction
            analysis.mergeStates(ourBlk->first(), calls);
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
    void compute(unsigned max_iter = 0, unsigned max_interproc_iter = 3) {
        LLVMValueRelationsAnalysis VRA(_M, max_iter);
        VRA.run(_blocks);

        while (--max_interproc_iter > 0) {
            // take computed relations and pass them into
            // called functions
            passCallSiteRelations(VRA);

            if (!VRA.run(_blocks))
                break; // fixpoint
        }
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

    auto getEquals(const llvm::Value *where, const llvm::Value *v)
        -> decltype(VRLocation::equalities.get(v)) {
        auto A = getMapping(where);
        assert(A);
        return A->equalities.get(v);
    }

    auto getEquals(const llvm::Value *where, const llvm::Value *v) const
        -> decltype(VRLocation::equalities.get(v)) {
        auto A = getMapping(where);
        assert(A);
        return A->equalities.get(v);
    }

    bool areEqual(const llvm::Value *where, const llvm::Value *v1, const llvm::Value *v2) const {
        if (v1 == v2)
            return true;

        auto A = getMapping(where);
        assert(A);
        if (auto E = A->equalities.get(v1))
            return E->count(v2) > 0;
        return false;
    }

    bool areEqual(const llvm::Value *v1, const llvm::Value *v2) const {
        return areEqual(v1, v1, v2) || areEqual(v2, v1, v2);
    }

    template <typename T>
    const T *
    getEqualValue(const llvm::Value *where, const llvm::Value *v) const {
        auto equals = getEquals(where, v);
        if (!equals)
            return nullptr;
        for (auto eq : *equals) {
            if (llvm::isa<T>(eq))
                return llvm::cast<T>(eq);
        }

        return nullptr;
    }

    std::vector<std::pair<const llvm::Value *, const llvm::Value *>>
    getReadsFromAlloca(const llvm::Value *where, const llvm::Value *v) const {
        auto A = getMapping(where);
        assert(A);

        auto equals = A->equalities.get(v);

        std::vector<std::pair<const llvm::Value *, const llvm::Value *>> ret;
        for (auto& it : A->reads) {
            if (it.first == v ||
                (equals && equals->count(v) > 0))
                ret.push_back(it);
            else if (auto GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(it.first)) {
                if (GEP->getPointerOperand() == v ||
                    (equals && equals->count(GEP->getPointerOperand()) > 0))
                    ret.push_back(it);
            }
        }

        return ret;
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_VALUE_RELATION_H_
