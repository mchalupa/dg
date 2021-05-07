#ifndef DG_POINTER_SUBGRAPH_OPTIMIZATIONS_H_
#define DG_POINTER_SUBGRAPH_OPTIMIZATIONS_H_

#include "PointsToMapping.h"

namespace dg {
namespace pta {

class PSNoopRemover {
    PointerGraph *G;

  public:
    PSNoopRemover(PointerGraph *g) : G(g) {}
    unsigned run();
};

// try to remove loads/stores that are provably
// loads and stores of unknown memory
// (these usually correspond to integers)
class PSUnknownsReducer {
    using MappingT = PointsToMapping<PSNode *>;

    PointerGraph *G;
    MappingT mapping;

    unsigned removed = 0;

    void processAllocs();

  public:
    PSUnknownsReducer(PointerGraph *g) : G(g) {}

    MappingT &getMapping() { return mapping; }
    const MappingT &getMapping() const { return mapping; }

    unsigned run() {
        processAllocs();
        return removed;
    };
};

class PSEquivalentNodesMerger {
  public:
    using MappingT = PointsToMapping<PSNode *>;

    PSEquivalentNodesMerger(PointerGraph *g) : G(g), merged_nodes_num(0) {
        mapping.reserve(32);
    }

    MappingT &getMapping() { return mapping; }
    const MappingT &getMapping() const { return mapping; }

    unsigned getNumOfMergedNodes() const { return merged_nodes_num; }

    unsigned run() {
        mergeCasts();
        return merged_nodes_num;
    }

  private:
    // get rid of all casts
    void mergeCasts();

    // merge node1 and node2 (node2 will be
    // the representant and node1 will be removed,
    // mapping will be set to  node1 -> node2)
    void merge(PSNode *node1, PSNode *node2);

    PointerGraph *G;
    // map nodes to its equivalent representant
    MappingT mapping;

    unsigned merged_nodes_num;
};

class PointerGraphOptimizer {
    using MappingT = PointsToMapping<PSNode *>;

    PointerGraph *G;
    MappingT mapping;

    unsigned removed = 0;

  public:
    PointerGraphOptimizer(PointerGraph *g) : G(g) {}

    void removeNoops() {
        PSNoopRemover remover(G);
        removed += remover.run();
    }

    void removeUnknowns() {
        PSUnknownsReducer reducer(G);
        if (auto r = reducer.run()) {
            mapping.merge(std::move(reducer.getMapping()));
            removed += r;
        }
    }

    void removeEquivalentNodes() {
        PSEquivalentNodesMerger merger(G);
        if (auto r = merger.run()) {
            mapping.merge(std::move(merger.getMapping()));
            removed += r;
        }
    }

    unsigned run() {
        removeNoops();
        removeEquivalentNodes();
        removeUnknowns();
        // need to call this once more because
        // the optimizations may have created
        // the same operands in a phi nodes,
        // which breaks the validity of the graph
        removeEquivalentNodes();

        return removed;
    }

    unsigned getNumOfRemovedNodes() const { return removed; }
    MappingT &getMapping() { return mapping; }
    const MappingT &getMapping() const { return mapping; }
};

} // namespace pta
} // namespace dg

#endif
