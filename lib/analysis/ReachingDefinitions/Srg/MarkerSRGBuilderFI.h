#ifndef _DG_MARKERSRGBUILDERFI_H
#define _DG_MARKERSRGBUILDERFI_H

#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <map>

#include "dg/analysis/legacy/BFS.h"
#include "analysis/ReachingDefinitions/Srg/SparseRDGraphBuilder.h"

namespace dg {
namespace analysis {
namespace rd {
namespace srg {

class MarkerSRGBuilderFI : public SparseRDGraphBuilder
{
    /* see using-s in SparseRDGraphBuilder for reference... */

    /* the resulting graph - stored in class for convenience, moved away on return */
    SparseRDGraph srg;
    /* phi nodes added during the process */
    std::vector<std::unique_ptr<NodeT>> phi_nodes;

    /* work structures */
    std::unordered_map<NodeT *, std::unordered_map<BlockT *, NodeT *>> current_def;
    std::unordered_map<NodeT *, std::unordered_map<BlockT *, NodeT *>> last_def;
    std::unordered_set<BlockT *> sealed_blocks;

    void writeVariable(const DefSite& var, NodeT *assignment, BlockT *block);
    NodeT *readVariableRecursive(const DefSite& var, BlockT *block);

    NodeT *readVariable(const DefSite& var, BlockT *read);
    void addPhiOperands(const DefSite& var, NodeT *phi, BlockT *block);

    void insertSrgEdge(NodeT *from, NodeT *to, const DefSite& var) {
        srg[from].push_back(std::make_pair(var, to));
    }

    void performLvn(BlockT *block) {
        for (NodeT *node : block->getNodes()) {

            for (const DefSite& def : node->defs) {
                last_def[def.target][block] = node;
            }
        }
    }

    void performGvn(BlockT *block) {
        for (NodeT *node : block->getNodes()) {

            for (const DefSite& use : node->getUses()) {
                NodeT *assignment = readVariable(use, block);
                // add edge from last definition to here
                if (assignment)
                    insertSrgEdge(assignment, node, use);
            }

            for (const DefSite& def : node->defs) {
                NodeT *assignment = readVariable(def, block);
                if (assignment)
                    insertSrgEdge(assignment, node, def);
                writeVariable(def, node, block);
            }
        }
    }

public:

    std::pair<SparseRDGraph, std::vector<std::unique_ptr<NodeT>>>
        build(NodeT *root) override {

        current_def.clear();

        legacy::BBlockBFS<NodeT> bfs(legacy::BFS_BB_CFG | legacy::BFS_INTERPROCEDURAL);

        AssignmentFinder af;
        af.populateUnknownMemory(root);

        std::vector<BlockT *> cfg;
        BlockT *block = root->getBBlock();
        bfs.run(block, [&](BlockT *block, void*){
            cfg.push_back(block);
        }, nullptr);

        for (BlockT *BB : cfg) {
            performLvn(BB);
        }

        for (BlockT *BB : cfg) {
            performGvn(BB);
        }

        return std::make_pair<SparseRDGraph, std::vector<std::unique_ptr<NodeT>>>(std::move(srg), std::move(phi_nodes));
    }

};

}
}
}
}

#endif /* _DG_MARKERSRGBUILDERFI_H */
