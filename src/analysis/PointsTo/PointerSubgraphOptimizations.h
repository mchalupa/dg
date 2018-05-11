#ifndef _DG_POINTER_SUBGRAPH_OPTIMIZATIONS_H_
#define _DG_POINTER_SUBGRAPH_OPTIMIZATIONS_H_

namespace dg {
namespace analysis {
namespace pta {

class PSNoopRemover {
    PointerSubgraph *PS;
public:
    PSNoopRemover(PointerSubgraph *PS) : PS(PS) {}

    unsigned run() {
        unsigned removed = 0;
        for (PSNode *nd : PS->getNodes()) {
            if (!nd)
                continue;

            if (nd->getType() == PSNodeType::NOOP) {
                nd->isolate();
                // this should not break the iterator
                PS->remove(nd);
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

// try to remove loads/stores that are provably
// loads and stores of unknown memory
// (these usually correspond to integers)
class PSUnknownsReducer {
    PointerSubgraph *PS;
    unsigned removed = 0;

    void processAllocs() {
        for (PSNode *nd : PS->getNodes()) {
            if (!nd)
                continue;

            if (nd->getType() == PSNodeType::ALLOC) {
                bool remove = true;
                for (PSNode *user : nd->getUsers()) {
                    // we store unknown to this memory
                    if (!isStoreOfUnknown(user, nd)
                        && user->getType() != PSNodeType::LOAD) {
                        remove = false;
                        break;
                    }
                }
                // this is an allocation that has only stores of unknown memory to it
                // (and its address is not stored anywhere) and there are only loads
                // from this memory (that result to unknown)
                if (remove) {
                    for (PSNode *user : nd->getUsers()) {
                        if (user->getType() == PSNodeType::LOAD) {
                            // replace the uses of the load value by unknown
                            // (this is what would happen in the analysis)
                            user->replaceAllUsesWith(UNKNOWN_MEMORY);
                        }
                        // store can be removed directly
                        user->isolate();
                        PS->remove(user);
                        ++removed;
                    }

                    nd->isolate();
                    PS->remove(nd);
                    ++removed;
                }
            }
        }
    }

public:
    PSUnknownsReducer(PointerSubgraph *PS) : PS(PS) {}

    unsigned run() {
        processAllocs();
        return removed;
    };
};


class PointerSubgraphOptimizer {
    PointerSubgraph *PS;
    unsigned removed = 0;
public:
    PointerSubgraphOptimizer(PointerSubgraph *PS) : PS(PS) {}

    void removeNoops() {
        PSNoopRemover remover(PS);
        removed += remover.run();
    }

    void removeUnknowns() {
        PSUnknownsReducer reducer(PS);
        removed += reducer.run();
    }

    unsigned getNumOfRemovedNodes() const { return removed; }
};


} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_POINTER_SUBGRAPH_OPTIMIZATIONS_H_
