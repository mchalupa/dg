#ifndef _DG_CYTRONSRGBUILDER_H
#define _DG_CYTRONSRGBUILDER_H

#include <map>
#include <unordered_map>
#include <vector>
#include <stack>

#include "analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "analysis/ReachingDefinitions/Srg/PhiPlacement.h"

namespace dg {
namespace analysis {
namespace rd {
namespace srg {

/**
 * SparseRDGraphBuilder based on algorithm introduced in
 * "An Efficient Method of Computing Static Single Assignment Form" [Cytron et al.]
 */
class CytronSRGBuilder : public SparseRDGraphBuilder
{
private:
    PhiAdditions phi;
    SparseRDGraph srg;

    // stack of last definitions for each variable
    std::unordered_map<NodeT *, StackT<NodeT *>> stacks;

    // for each assignment contains variables modified by setting its LHS
    std::unordered_map<NodeT *, std::vector<VarT>> oldLHS;

    void constructSrg(BlockT *root_block)
    {
        // restart state
        stacks.clear();
        oldLHS.clear();

        search(root_block);
    }

    void addAssignment(NodeT *assignment, const VarT& var)
    {
        oldLHS[assignment].push_back(var);

        if (!stacks[var.target].empty()) {
            srg[stacks[var.target].top()].push_back(std::make_pair(var, assignment));
        }

        stacks[var.target].push(assignment);
    }

    void addUse(NodeT *use, const VarT& var)
    {
        if (!stacks[var.target].empty())
            srg[stacks[var.target].top()].push_back(std::make_pair(var, use));
    }

    void search(BlockT *X)
    {
        // find assignments in block
        for (NodeT *A : X->getNodes())
        {
            if (A->getType() != RDNodeType::PHI) {
                for (const DefSite& use : A->getUses())
                {
                    addUse(A, use);
                }
            }
            for (const DefSite& def : A->getDefines())
            {
                addAssignment(A, def);
            }
        }

        // add individual phi-functions as uses
        for (auto& edge : X->successors())
        {
            BlockT *Y = edge.target;
            for (NodeT *F : Y->getNodes())
            {
                if (F->getType() != RDNodeType::PHI)
                    continue;

                for (const DefSite& use : F->getUses()) {
                    addUse(F, use);
                }
            }
        }

        for (BlockT *Y : X->getDominators())
        {
            search(Y);
        }

        for (NodeT *A : X->getNodes())
        {
            for (VarT& var : oldLHS[A])
            {
                stacks[var.target].pop();
            }
        }
    }


public:
    /**
     * see: SparseRDGraphBuilder::build
     */
    std::pair<SparseRDGraph, std::vector<std::unique_ptr<NodeT>>>
        build(NodeT *root) override
    {
        assert( root && "need root" );

        // find assignments and use them to find places for phi-functions
        AssignmentFinder af;
        // add def-use edges for unknown memory
        af.populateUnknownMemory(root);

        PhiPlacement pp;

        // place the phi functions into program
        std::vector<std::unique_ptr<NodeT>> phi_nodes = pp.place(pp.calculate(af.build(root)));

        // now recursively construct the SparseRDGraph
        constructSrg(root->getBBlock());

        return std::make_pair(std::move(srg), std::move(phi_nodes));
    }

};

}
}
}
}
#endif /* _DG_CYTRONSRGBUILDER_H */
