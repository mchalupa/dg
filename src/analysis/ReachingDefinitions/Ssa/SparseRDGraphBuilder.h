#ifndef _DG_SSABUILDER_H_
#define _DG_SSABUILDER_H_

#include <map>
#include <unordered_map>
#include <vector>
#include <stack>

#include "BBlock.h"
#include "analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "analysis/ReachingDefinitions/Ssa/PhiPlacement.h"

namespace dg {
namespace analysis {
namespace rd {
namespace ssa {

/**
 * Calculates a Sparse Graph for RD information propagation
 */
class SparseRDGraphBuilder
{
private:
    using NodeT = dg::analysis::rd::RDNode;
    using BlockT = BBlock<NodeT>;

    using VarT = DefSite*;

    // just for convenience
    template <typename _Tp> using StackT = std::stack<_Tp, std::vector<_Tp>>;

public:
    using SRGEdge = std::pair<VarT, NodeT*>;
    // neighbour lists representation of Sparse Graph
    //                                       ALLOCA             pair(Variable, Def/Use)
    using SparseRDGraph = std::unordered_map<NodeT *, std::vector<SRGEdge>>;

private:
    PhiAdditions phi;
    SparseRDGraph srg;

    // stack of last definitions for each variable
    std::unordered_map<NodeT *, StackT<NodeT *>> stacks;

    // for each assignment contains variables modified by setting its LHS
    std::unordered_map<NodeT *, std::vector<VarT>> oldLHS;

    std::vector<std::unique_ptr<NodeT>> phi_nodes;

    void constructSrg(BlockT *root_block)
    {
        // restart state
        stacks.clear();

        search(root_block);
    }

    void addAssignment(NodeT *assignment, VarT var)
    {
        oldLHS[assignment].push_back(var);

        if (!stacks[var->target].empty()) {
            srg[stacks[var->target].top()].push_back(std::make_pair(var, assignment));
        }

        stacks[var->target].push(assignment);
    }

    void addUse(NodeT *use, VarT var)
    {
        if (!stacks[var->target].empty())
            srg[stacks[var->target].top()].push_back(std::make_pair(var, use));
    }

    void search(BlockT *X)
    {

        // find assignments in block
        for (NodeT *A : X->getNodes())
        {
            for (const DefSite& cds : A->defs)
            {
                VarT var = const_cast<VarT>(&cds);
                addAssignment(A, var);
            }
            for (const DefSite& use : A->uses)
            {
                VarT var = const_cast<VarT>(&use);
                addUse(A, var);
            }
        }

        // add individual phi-functions as uses
        for (auto& edge : X->successors())
        {
            BlockT *Y = edge.target;
            for (NodeT *F : Y->getNodes())
            {
                for (const DefSite& use : F->getUses()) {
                    VarT var = const_cast<VarT>(&use);
                    addUse(F, var);
                }
            }
        }

        for (BlockT *Y : X->getDominators())
        {
            search(Y);
        }

        for (NodeT *A : X->getNodes())
        {
            for (VarT var : oldLHS[A])
            {
                stacks[var->target].pop();
            }
        }
    }

public:

    /**
     * Builds a sparse graph
     */
    SparseRDGraph&& build(NodeT *root)
    {
        assert( root && "need root" );

        // find assignments and use them to find places for phi-functions
        AssignmentFinder af;
        PhiPlacement pp;
        phi = pp.calculate(af.build(root));

        // place the phi functions into program
        phi_nodes = pp.place(phi);

        // now recursively construct the SparseRDGraph
        constructSrg(root->getBBlock());

        return std::move(srg);
    }
};

using SparseRDGraph = SparseRDGraphBuilder::SparseRDGraph;

}
}
}
}
#endif /* _DG_SSABUILDER_H_ */
