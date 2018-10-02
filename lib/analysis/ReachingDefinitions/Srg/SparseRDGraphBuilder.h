#ifndef _DG_SRGBUILDER_H_
#define _DG_SRGBUILDER_H_

#include <map>
#include <unordered_map>
#include <vector>
#include <stack>

#include "dg/BBlock.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "analysis/ReachingDefinitions/Srg/PhiPlacement.h"

namespace dg {
namespace analysis {
namespace rd {
namespace srg {

/**
 * Calculates a Sparse Graph for RD information propagation
 */
class SparseRDGraphBuilder
{
public:
    using NodeT = dg::analysis::rd::RDNode;
    using BlockT = BBlock<NodeT>;

    using VarT = DefSite;

    // just for convenience
    template <typename _Tp> using StackT = std::stack<_Tp, std::vector<_Tp>>;

    using SRGEdge = std::pair<VarT, NodeT*>;
    // neighbour lists representation of Sparse Graph
    //                                       ALLOCA             pair(Variable, Def/Use)
    using SparseRDGraph = std::unordered_map<NodeT *, std::vector<SRGEdge>>;

    virtual ~SparseRDGraphBuilder() = default;

    /**
     * Builds a sparse graph
     * Return value: the graph plus ownership of added phi nodes
     */
    virtual std::pair<SparseRDGraph, std::vector<std::unique_ptr<NodeT>>>
        build(NodeT *root) = 0;

};

using SparseRDGraph = SparseRDGraphBuilder::SparseRDGraph;

}
}
}
}
#endif /* _DG_SRGBUILDER_H_ */
