#ifndef _DG_EQUIVALENT_NODES_MERGER_H_
#define _DG_EQUIVALENT_NODES_MERGER_H_

#include <unordered_map>
#include <cassert>

namespace dg {
namespace analysis {
namespace pta {

class PSEquivalentNodesMerger {
public:
    using MappingT = std::unordered_map<PSNode *, PSNode *>;

    PSEquivalentNodesMerger(PointerSubgraph *S)
    : PS(S), merged_nodes_num(0) {
        mapping.reserve(32);
    }

    MappingT& getMapping() { return mapping; }
    const MappingT& getMapping() const { return mapping; }

    MappingT& mergeNodes() {
        auto nodes = PS->getNodes();
        for (PSNode *node : nodes) {
            if (!node) // id 0
                continue;

            // bitcast of alloca will always point to that alloca
            // (it is a must alias)
            if (node->getType() == PSNodeType::CAST
                && node->getOperand(0)->getType() == PSNodeType::ALLOC)
                merge(node, node->getOperand(0));
        }

        return mapping;
    }

    unsigned getMergedNodesNum() const {
        return merged_nodes_num;
    }

private:
    // merge node1 and node2 (node2 will be
    // the representant and node1 will be removed,
    // mapping will be se node1->node2)
    void merge(PSNode *node1, PSNode *node2) {
        // remove node1
        node1->replaceAllUsesWith(node2);
        node1->isolate();
        // XXX: the builder must have a reference to the node1
        // or we would leak it

        // update the mapping
        auto it = mapping.find(node1);
        assert(it == mapping.end());
        mapping.emplace_hint(it, node1, node2);

        ++merged_nodes_num;
    }

    PointerSubgraph *PS;
    // map nodes to its equivalent representant
    MappingT mapping;

    unsigned merged_nodes_num;
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_EQUIVALENT_NODES_MERGER_H_
