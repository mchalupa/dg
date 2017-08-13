#ifndef SSABUILDER_H
#define SSABUILDER_H

#include "BBlock.h"
#include "analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "analysis/DominanceFrontiers.h"
#include "llvm/analysis/Dominators.h"

namespace dg {
namespace analysis {
namespace ssa {
/**
 * Transforms given program to its Static Single Assignment form
 */
class SsaBuilder
{
    using NodeT = dg::analysis::rd::RDNode;
    using BlockT = BBlock<NodeT>;
    using MapT = std::unordered_map<const llvm::Function *, std::map<const llvm::BasicBlock *, BlockT *>>;

public:

    /**
     * Transforms given program to its Ssa Form
     * this will create a new graph with root given as unique_ptr
     * the caller is responsible for deallocation of the resulting graph
     */
    BlockT *build(const BlockT *root, MapT& constructed_functions)
    {
        // calculate dominators, true=calculate also DomFrontiers
        Dominators<NodeT,true> d;
        d.calculate(constructed_functions);

        return nullptr;
    }
};

}
}
}
#endif /* SSABUILDER_H */
