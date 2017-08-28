#ifndef _DG_SSABUILDER_H_
#define _DG_SSABUILDER_H_

#include <map>
#include <unordered_map>
#include <vector>
#include <stack>

#include "BBlock.h"
#include "analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "analysis/DominanceFrontiers.h"
#include "llvm/analysis/Dominators.h"
#include "analysis/ReachingDefinitions/Ssa/PhiPlacement.h"

namespace dg {
namespace analysis {
namespace rd {
namespace ssa {

/**
 * Transforms given program to its Static Single Assignment form
 */
class SsaBuilder
{
private:

    using NodeT = dg::analysis::rd::RDNode;
    using BlockT = BBlock<NodeT>;
    using CFMapT = std::unordered_map<const llvm::Function *, std::map<const llvm::BasicBlock *, BlockT *>>;

    template <typename _Tp> using StackT = std::stack<_Tp, std::vector<_Tp>>;

    PhiAdditions phi;
    std::unordered_map<NodeT *, size_t> counters;
    std::unordered_map<NodeT *, StackT<size_t>> stacks;

    void constructSsa(BlockT *root_block)
    {
        counters.clear();
        stacks.clear();

        search(root_block);
    }

    void search(BlockT *X)
    {
        std::unordered_map<NodeT *, NodeT *> oldLHS;
        for (NodeT *A : X->getNodes())
        {
            if (A->getType() == RDNodeType::STORE) {
                // A is an ordinary assignment...
                // TODO: for each variable used in RHS???
            }
        }

        for (auto& edge : X->successors())
        {
            BlockT *Y = edge.target;
        }

        for (BlockT *Y : X->getDominators())
        {
            search(Y);
        }

        for (NodeT *A : X->getNodes())
        {
            if (A->getType() != RDNodeType::STORE)
                continue;
        }
    }
public:

    /**
     * Transforms given program to its Ssa Form
     * this will create a new graph with root given as unique_ptr
     * the caller is responsible for deallocation of the resulting graph
     */
    NodeT *build(NodeT *root, CFMapT& constructed_functions)
    {
        // calculate dominators, true=calculate also DomFrontiers
        Dominators<NodeT,true> d;
        d.calculate(constructed_functions);

        AssignmentFinder af;

        PhiPlacement pp;
        phi = pp.calculate(af.build(root));

        return nullptr;
    }

    const PhiAdditions& getPhi() const { return phi; }
};

}
}
}
}
#endif /* _DG_SSABUILDER_H_ */
