#ifndef _DG_DOMINATORS_H_
#define _DG_DOMINATORS_H_

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Function.h>
#include <llvm/IR/Dominators.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "BBlock.h"
#include "analysis/DominanceFrontiers.h"

namespace dg {
namespace analysis {
namespace rd {
namespace ssa {


/**
 * Calculates dominators
 */
template <typename NodeT,bool CalculateDF=true>
class Dominators
{
private:
    using BlockT = BBlock<dg::analysis::rd::RDNode>;
    using MapT = std::unordered_map<const llvm::Function *, std::map<const llvm::BasicBlock *, BlockT *>>;

public:
    void calculate(MapT& functions_blocks)
    {
        using namespace llvm;

        for (auto& pair : functions_blocks){

            BlockT *root = nullptr;
            Function& f = *const_cast<Function *>(pair.first);

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
            DominatorTree dt;
            // compute post-dominator tree for this function
            dt.runOnFunction(f);
#else
            DominatorTreeAnalysis PDT;
            FunctionAnalysisManager DummyFAM;
            DominatorTree dt(PDT.run(f, DummyFAM));
#endif
            auto& blocks = pair.second;
            bool built = false;
            for (auto& block : blocks) {
                BasicBlock *llvm_block = const_cast<BasicBlock *>(block.first);
                BlockT *basic_block = block.second;

                DomTreeNode *N = dt.getNode(llvm_block);
                if (!N)
                    continue;

                DomTreeNode *idom = N->getIDom();
                BasicBlock *idomBB = idom ? idom->getBlock() : nullptr;
                built = true;

                if (idomBB) {
                    BlockT *db = blocks[idomBB];
                    assert(db && "Do not have constructed BB");
                    basic_block->setIDom(db);
                } else {
                    if (!root) {
                        root = new BlockT();
                        root->setKey(nullptr);
                    }

                    basic_block->setIDom(root);
                }
            }

            if (CalculateDF) {
                analysis::DominanceFrontiers<NodeT> dfrontiers;
                if (root)
                    dfrontiers.compute(root);
            }
        }
    }
};

}
}
}
}

#endif /* _DG_DOMINATORS_H_ */
