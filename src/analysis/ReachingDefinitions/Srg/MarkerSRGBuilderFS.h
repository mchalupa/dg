#ifndef _DG_MARKERSRGBUILDERFS_H
#define _DG_MARKERSRGBUILDERFS_H

#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <map>

#include "analysis/BFS.h"
#include "analysis/ReachingDefinitions/Srg/SparseRDGraphBuilder.h"
#include "analysis/ReachingDefinitions/Srg/IntervalMap.h"

namespace dg {
namespace analysis {
namespace rd {
namespace srg {

class MarkerSRGBuilderFS : public SparseRDGraphBuilder
{
    /* see using-s in SparseRDGraphBuilder for reference... */

    // offset in variable
    using OffsetT = uint64_t;
    using Intervals = std::vector<detail::Interval>;

    // for each variable { for each block { for each offset in variable { remember definition } } }
    using DefMapT = std::unordered_map<NodeT *, std::unordered_map<BlockT *, detail::IntervalMap<NodeT *>>>;

    /* the resulting graph - stored in class for convenience, moved away on return */
    SparseRDGraph srg;

    /* phi nodes added during the process */
    std::vector<std::unique_ptr<NodeT>> phi_nodes;

    /* work structures for strong defs */
    DefMapT current_def;
    DefMapT last_def;

    /* work structures for weak defs */
    DefMapT current_weak_def;
    DefMapT last_weak_def;

    /**
     * Remember strong definition @assignment of a @var in @block.
     * Side-effect: kill current overlapping strong definitions and current overlapping weak definitions.
     */
    void writeVariableStrong(const DefSite& var, NodeT *assignment, BlockT *block);

    /**
     * Remember weak definition @assignment of @var in @block.
     * Does not affect other definitions.
     */
    void writeVariableWeak(const DefSite& var, NodeT *assignment, BlockT *block);

    /**
     * Recursively looks up definition of @var in @block starting in @start. @start is supplied to prevent infinite recursion with weak updates.
     * @covered is set of intervals where strong update has already been found.
     * Returns a phi node that joins previous definitions. 
     * The phi node is owned by the @phi_nodes vector.
     */
    NodeT *readVariableRecursive(const DefSite& var, BlockT *block, BlockT *start, const Intervals& covered);

    /*
     * If the interval has unknown offset or length, it is changed to contain everything.
     * Optional parameter @size makes it possible to concretize to variable size, in case the size is known.
     */
    detail::Interval concretize(detail::Interval interval, uint64_t size = Offset::UNKNOWN) const {
        if (size == 0) {
            size = Offset::UNKNOWN;
        }
        if (interval.isUnknown()) {
            return detail::Interval{ 0, size };
        }
        return interval;
    }

    std::vector<NodeT *> readVariable(const DefSite& var, BlockT *read, BlockT *start) {
        Intervals empty_vector;
        return readVariable(var, read, start, empty_vector);
    }

    /**
     * Lookup all definitions of @var in @read starting from @start.
     */
    std::vector<NodeT *> readVariable(const DefSite& var, BlockT *read, BlockT *start, const Intervals& covered);

    void addPhiOperands(const DefSite& var, NodeT *phi, BlockT *block, BlockT *start, const Intervals& covered);

    /**
     * Insert a def->use edge into the resulting SparseRDGraph.
     * @from is a definition
     * @to is a use
     */
    void insertSrgEdge(NodeT *from, NodeT *to, const DefSite& var) {
        srg[from].push_back(std::make_pair(var, to));
    }

    void performLvn(BlockT *block) {
        for (NodeT *node : block->getNodes()) {
            for (const DefSite& def : node->defs) {
                if (node->isOverwritten(def) && !def.offset.isUnknown()) {
                    detail::Interval interval = concretize(detail::Interval{def.offset, def.len}, def.target->getSize());
                    last_def[def.target][block].killOverlapping(interval);
                    last_weak_def[def.target][block].killOverlapping(interval);
                    last_def[def.target][block].add(std::move(interval), node);
                } else {
                    last_weak_def[def.target][block].add(concretize(detail::Interval{def.offset, def.len}, def.target->getSize()), node);
                }
            }
        }
    }

    void performGvn(BlockT *block) {
        for (NodeT *node : block->getNodes()) {
            for (const DefSite& use : node->getUses()) {
                std::vector<NodeT *> assignments = readVariable(use, block, block);
                // add edge from last definition to here
                for (NodeT *assignment : assignments) {
                    insertSrgEdge(assignment, node, use);
                }
            }

            for (const DefSite& def : node->defs) {
                if (node->isOverwritten(def) && !def.offset.isUnknown()) {
                    writeVariableStrong(def, node, block);
                } else {
                    writeVariableWeak(def, node, block);
                }
            }
        }
    }

public:

    std::pair<SparseRDGraph, std::vector<std::unique_ptr<NodeT>>>
        build(NodeT *root) override {

        current_def.clear();

        BBlockBFS<NodeT> bfs(BFS_BB_CFG | BFS_INTERPROCEDURAL);

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

#endif /* _DG_MARKERSRGBUILDERFS_H */
