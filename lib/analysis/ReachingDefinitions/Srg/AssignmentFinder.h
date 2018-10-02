#ifndef _DG_ASSIGNMENTFINDER_H_
#define _DG_ASSIGNMENTFINDER_H_

#include <unordered_map>
#include <vector>

#include "dg/analysis/SubgraphNode.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"

namespace dg {
namespace analysis {
namespace rd {
namespace srg {

using AssignmentMap = std::unordered_map<dg::analysis::rd::RDNode *, std::vector<dg::analysis::rd::RDNode *>>;

/**
 * Constructs Def->Use graph from RDNode-s
 */
class AssignmentFinder
{
private:
    unsigned DFS = 10;
    using NodeT = dg::analysis::rd::RDNode;

    /**
     * Returns all nodes from graph with given root in BFS order
     */
    std::vector<NodeT *> bfs(NodeT *root)
    {
        assert(root && "need root");
        std::vector<NodeT *> result;

        unsigned dfsnum = DFS++;

        ADT::QueueFIFO<NodeT *> fifo;
        fifo.push(root);
        root->dfsid = dfsnum;

        while (!fifo.empty()) {
            NodeT *cur = fifo.pop();
            result.push_back(cur);

            for (NodeT *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    fifo.push(succ);
                }
            }
        }

        return result;
    }

    bool isAllocation(NodeT *node)
    {
        return node->getType() == RDNodeType::ALLOC;
    }

public:

    /**
     * Finds all variables in the program and then:
     * + For each definition of unknown memory: add possible definition of the variable to RDNode
     * + For each use of unknown memory: add possible use of the variable to RDNode
     */
    void populateUnknownMemory(NodeT *root)
    {
        assert(root && "root may not be null");
        std::vector<NodeT *> cfg = bfs(root);

        std::vector<NodeT *> allocas;
        // find all variables
        for (auto& def : cfg) {
            if (isAllocation(def))
                allocas.push_back(def);
        }

        for (auto& use : cfg) {
            // if this node does not use any memory, skip it
            if (use->defs.size() + use->uses.size() == 0)
                continue;

            for (auto& def : allocas) {
                // node that defines UNKNOWN_MEMORY can potentially define this def
                if (use->defines(UNKNOWN_MEMORY))
                    use->addDef(def);

                // node that uses UNKNOWN_MEMORY can potentially use this def
                if (use->usesUnknown())
                    use->addUse(def);
            }
        }
    }
    /**
     * For each alloca node, finds all assignments to it
     */
    AssignmentMap build(NodeT *root)
    {
        assert(root && "root may not be null");
        AssignmentMap result;
        std::vector<NodeT *> cfg = bfs(root);
        for (auto& def : cfg) {
            if (!isAllocation(def))
                continue;

            std::vector<NodeT *> uses;
            for (auto& use : cfg) {
                if (use->defines(def) || (use->defs.size() > 0 && use->defines(UNKNOWN_MEMORY))) {
                    uses.push_back(use);
                }
            }
            result[def] = std::move(uses);
        }
        return result;
    }
};

}
}
}
}

#endif // _DG_ASSIGNMENTFINDER_H_
