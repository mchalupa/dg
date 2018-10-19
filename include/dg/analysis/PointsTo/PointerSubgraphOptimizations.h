#ifndef _DG_POINTER_SUBGRAPH_OPTIMIZATIONS_H_
#define _DG_POINTER_SUBGRAPH_OPTIMIZATIONS_H_

#include "PointsToMapping.h"

namespace dg {
namespace analysis {
namespace pta {

class PSNoopRemover {
    PointerSubgraph *PS;
public:
    PSNoopRemover(PointerSubgraph *PS) : PS(PS) {}

    unsigned run() {
        unsigned removed = 0;
        for (const auto &nd : PS->getNodes()) {
            if (!nd)
                continue;

            if (nd->getType() == PSNodeType::NOOP) {
                nd->isolate();
                // this should not break the iterator
                PS->remove(nd.get());
                ++removed;
            }
        }
        return removed;
    };
};

static inline bool isStoreOfUnknown(PSNode *S, PSNode *to) {
    return (S->getType() == PSNodeType::STORE &&
            S->getOperand(1) == to &&
            S->getOperand(0)->isUnknownMemory());
}

static inline bool usersImplyUnknown(PSNode *nd) {
    for (PSNode *user : nd->getUsers()) {
        // we store only unknown to this memory
        if (!isStoreOfUnknown(user, nd) &&
            user->getType() != PSNodeType::LOAD)
            return false;
    }

    return true;
}

static inline bool allOperandsAreSame(PSNode *nd) {
    auto opNum = nd->getOperandsNum();
    if (opNum < 1)
        return true;

    PSNode *op0 = nd->getOperand(0);
    for (decltype(opNum) i = 1; i < opNum; ++i) {
        if (op0 != nd->getOperand(i))
            return false;
    }

    return true;
}

// try to remove loads/stores that are provably
// loads and stores of unknown memory
// (these usually correspond to integers)
class PSUnknownsReducer {
    using MappingT = PointsToMapping<PSNode *>;

    PointerSubgraph *PS;
    MappingT mapping;

    unsigned removed = 0;

    void processAllocs() {
        for (const auto& nd : PS->getNodes()) {
            if (!nd)
                continue;

            if (nd->getType() == PSNodeType::ALLOC) {
                // this is an allocation that has only stores of unknown memory to it
                // (and its address is not stored anywhere) and there are only loads
                // from this memory (that must result to unknown)
                if (usersImplyUnknown(nd.get())) {
                    for (PSNode *user : nd->getUsers()) {
                        if (user->getType() == PSNodeType::LOAD) {
                            // replace the uses of the load value by unknown
                            // (this is what would happen in the analysis)
                            user->replaceAllUsesWith(UNKNOWN_MEMORY);
                            mapping.add(user, UNKNOWN_MEMORY);
                        }
                        // store can be removed directly
                        user->isolate();
                        PS->remove(user);
                        ++removed;
                    }

                    // NOTE: keep the alloca, as it contains the
                    // pointer to itself and may be queried for this pointer
                }
            } else if (nd->getType() == PSNodeType::PHI && nd->getOperandsNum() == 0) {
                for (PSNode *user : nd->getUsers()) {
                    // replace the uses of this value with unknown
                    user->replaceAllUsesWith(UNKNOWN_MEMORY);
                    mapping.add(user, UNKNOWN_MEMORY);

                    // store can be removed directly
                    user->isolate();
                    PS->remove(user);
                    ++removed;
                }

                nd->isolate();
                PS->remove(nd.get());
                assert(nd.get() == nullptr);
                ++removed;
            }
        }
    }

public:
    PSUnknownsReducer(PointerSubgraph *PS) : PS(PS) {}

    MappingT& getMapping() { return mapping; }
    const MappingT& getMapping() const { return mapping; }

    unsigned run() {
        processAllocs();
        return removed;
    };
};

class PSEquivalentNodesMerger {
public:
    using MappingT = PointsToMapping<PSNode *>;

    PSEquivalentNodesMerger(PointerSubgraph *S)
    : PS(S), merged_nodes_num(0) {
        mapping.reserve(32);
    }

    MappingT& getMapping() { return mapping; }
    const MappingT& getMapping() const { return mapping; }

    unsigned getNumOfMergedNodes() const {
        return merged_nodes_num;
    }

    unsigned run() {
        mergeCasts();
        return merged_nodes_num;
    }

private:
    // get rid of all casts
    void mergeCasts() {
        for (const auto& nodeptr : PS->getNodes()) {
            if (!nodeptr)
                continue;

            PSNode *node = nodeptr.get();

            // cast is always 'a proxy' to the real value,
            // it does not change the pointers
            if (node->getType() == PSNodeType::CAST)
                merge(node, node->getOperand(0));
            else if (PSNodeGep *GEP = PSNodeGep::get(node)) {
                if (GEP->getOffset().isZero()) // GEP with 0 offest is cast
                    merge(node, GEP->getSource());
            } else if (node->getType() == PSNodeType::PHI &&
                        node->getOperandsNum() > 0 && allOperandsAreSame(node)) {
                merge(node, node->getOperand(0));
            }
        }
    }

    // merge node1 and node2 (node2 will be
    // the representant and node1 will be removed,
    // mapping will be set to  node1 -> node2)
    void merge(PSNode *node1, PSNode *node2) {
        // remove node1
        node1->replaceAllUsesWith(node2);
        node1->isolate();
        PS->remove(node1);

        // update the mapping
        mapping.add(node1, node2);

        ++merged_nodes_num;
    }

    PointerSubgraph *PS;
    // map nodes to its equivalent representant
    MappingT mapping;

    unsigned merged_nodes_num;
};

class PointerSubgraphOptimizer {
    using MappingT = PointsToMapping<PSNode *>;

    PointerSubgraph *PS;
    MappingT mapping;

    unsigned removed = 0;
public:
    PointerSubgraphOptimizer(PointerSubgraph *PS) : PS(PS) {}

    void removeNoops() {
        PSNoopRemover remover(PS);
        removed += remover.run();
    }

    void removeUnknowns() {
        PSUnknownsReducer reducer(PS);
        if (auto r = reducer.run()) {
            mapping.merge(std::move(reducer.getMapping()));
            removed += r;
        }
    }

    void removeEquivalentNodes() {
        PSEquivalentNodesMerger merger(PS);
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
    MappingT& getMapping() { return mapping; }
    const MappingT& getMapping() const { return mapping; }
};


} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_POINTER_SUBGRAPH_OPTIMIZATIONS_H_
