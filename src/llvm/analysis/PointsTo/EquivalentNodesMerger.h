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
        mergeCasts();
        return mapping;
    }

    unsigned getNumOfMergedNodes() const {
        return merged_nodes_num;
    }

private:
    // get rid of all casts
    void mergeCasts() {
        auto nodes = PS->getNodes();
        for (PSNode *node : nodes) {
            if (!node)
                continue;

            // cast is always 'a proxy' to the real value,
            // it does not change the pointers
            if (node->getType() == PSNodeType::CAST)
                merge(node, node->getOperand(0));
            else if (PSNodeGep *GEP = PSNodeGep::get(node)) {
                if (GEP->getOffset().isZero()) // GEP with 0 offest is cast
                    merge(node, GEP->getSource());
            }
        }
    }

    // merge node1 and node2 (node2 will be
    // the representant and node1 will be removed,
    // mapping will be se node1->node2)
    void merge(PSNode *node1, PSNode *node2) {
        // remove node1
        node1->replaceAllUsesWith(node2);
        node1->isolate();
        // remove the node, we won't need it anymore
        PS->remove(node1);

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
