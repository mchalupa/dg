#ifndef _DG_DOMINATORS_H_
#define _DG_DOMINATORS_H_

#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>

#include "BBlock.h"
#include "analysis/DominanceFrontiers.h"

namespace dg {
namespace analysis {

/**
 * Calculates dominators using LLVM framework
 * Template parameters:
 *  NodeT
 *  CalculateDF = should dominance frontiers be calculated, too?
 */
template <typename NodeT, bool CalculateDF = true>
class Dominators {
  private:
    using BlockT = BBlock<dg::analysis::rd::RWNode>;
    using CFMapT =
            std::unordered_map<const llvm::Function *,
                               std::map<const llvm::BasicBlock *, BlockT *>>;
    using BMapT =
            std::unordered_map<const llvm::Value *, std::unique_ptr<BlockT>>;

  public:
    void calculate(CFMapT &functions_blocks, const BMapT &all_blocks) {
        using namespace llvm;

        for (auto &pair : functions_blocks) {
            Function &f = *const_cast<Function *>(pair.first);

            DominatorTree dt;
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
            // compute dominator tree for this function
            dt.runOnFunction(f);
#else
            dt.recalculate(f);
#endif
            auto it = all_blocks.find(dt.getRoot());
            assert(it != all_blocks.end() && "root block must exist");
            BlockT *root = it->second.get();
            auto &blocks = pair.second;
            for (auto &block : blocks) {
                BasicBlock *llvm_block = const_cast<BasicBlock *>(block.first);
                BlockT *basic_block = block.second;

                DomTreeNode *N = dt.getNode(llvm_block);
                if (!N)
                    continue;

                DomTreeNode *idom = N->getIDom();
                BasicBlock *idomBB = idom ? idom->getBlock() : nullptr;

                for (const auto &dom : N->getChildren()) {
                    const BasicBlock *dom_llvm_block = dom->getBlock();
                    const auto it = all_blocks.find(
                            static_cast<const llvm::Value *>(dom_llvm_block));
                    assert(it != all_blocks.end() &&
                           "Do not have constructed domBB");
                    const BlockT *dom_block = it->second.get();
                    if (dom_block != root)
                        basic_block->addDominator(
                                const_cast<BlockT *>(dom_block));
                }

                if (idomBB) {
                    auto it = all_blocks.find(idomBB);
                    assert(it != all_blocks.end() &&
                           "Do not have constructed BB");
                    BlockT *db = it->second.get();
                    basic_block->setIDom(db);
                } else {
                    if (basic_block != root)
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

} // namespace analysis
} // namespace dg

#endif /* _DG_DOMINATORS_H_ */
