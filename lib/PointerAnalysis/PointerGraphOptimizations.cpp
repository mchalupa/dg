#include "dg/PointerAnalysis/PointerGraphOptimizations.h"
#include "dg/PointerAnalysis/PointerGraph.h"

namespace dg {
namespace pta {

static inline bool isStoreOfUnknown(PSNode *S, PSNode *to) {
    return (S->getType() == PSNodeType::STORE && S->getOperand(1) == to &&
            S->getOperand(0)->isUnknownMemory());
}

static inline bool usersImplyUnknown(PSNode *alloc) {
    assert(alloc->getType() == PSNodeType::ALLOC);

    for (PSNode *user : alloc->getUsers()) {
        // we store only unknown to this memory
        // and the only other thing that we do is that
        // we load from it
        if ((user->getType() != PSNodeType::LOAD) &&
            // 'user' is not store of 'unknown' to 'nd'
            !isStoreOfUnknown(user, alloc))
            return false;
    }

    return true;
}

void removeNode(PointerGraph *G, PSNode *nd) {
    // llvm::errs() << "Remove node " << nd->getID() << "\n";

    assert(nd->getUsers().empty() && "Removing node that has users");
    // remove from CFG
    nd->isolate();
    // clear its operands (so that the operands do not
    // have a dangling reference to this node in 'users')
    nd->removeAllOperands();
    // delete it from the graph
    G->remove(nd);
}

void PSUnknownsReducer::processAllocs() {
    for (const auto &nd : G->getNodes()) {
        if (!nd)
            continue;

        if (nd->getType() == PSNodeType::ALLOC) {
            // this is an allocation that has only stores of unknown memory to
            // it (and its address is not stored anywhere) and there are only
            // loads from this memory (that must result to unknown)
            if (usersImplyUnknown(nd.get())) {
                // create a copy of users, as we will modify the container
                auto tmp = nd->getUsers();
                for (PSNode *user : tmp) {
                    if (user->getType() == PSNodeType::LOAD) {
                        // replace the uses of the load value by unknown
                        // (this is what would happen in the analysis)
                        user->replaceAllUsesWith(UNKNOWN_MEMORY);
                        mapping.add(user, UNKNOWN_MEMORY);
                    }
                    // store can be removed directly
                    removeNode(G, user);
                    ++removed;
                }

                // NOTE: keep the alloca, as it contains the
                // pointer to itself and may be queried for this pointer
            }
        } else if (nd->getType() == PSNodeType::PHI &&
                   nd->getOperandsNum() == 0) {
            auto tmp = nd->getUsers();
            for (PSNode *user : tmp) {
                // replace the uses of this value with unknown
                user->replaceAllUsesWith(UNKNOWN_MEMORY);
                mapping.add(user, UNKNOWN_MEMORY);

                // store can be removed directly
                removeNode(G, user);
                ++removed;
            }

            removeNode(G, nd.get());
            ++removed;
        }
    }
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

// get rid of all casts
void PSEquivalentNodesMerger::mergeCasts() {
    for (const auto &nodeptr : G->getNodes()) {
        if (!nodeptr)
            continue;

        PSNode *node = nodeptr.get();

        // cast is always 'a proxy' to the real value,
        // it does not change the pointers
        if (node->getType() == PSNodeType::CAST ||
            (node->getType() == PSNodeType::PHI && node->getOperandsNum() > 0 &&
             allOperandsAreSame(node))) {
            merge(node, node->getOperand(0));
        } else if (PSNodeGep *GEP = PSNodeGep::get(node)) {
            if (GEP->getOffset().isZero()) // GEP with 0 offest is cast
                merge(node, GEP->getSource());
        }
    }
}

void PSEquivalentNodesMerger::merge(PSNode *node1, PSNode *node2) {
    // remove node1
    node1->replaceAllUsesWith(node2);
    removeNode(G, node1);

    // update the mapping
    mapping.add(node1, node2);

    ++merged_nodes_num;
}

unsigned PSNoopRemover::run() {
    unsigned removed = 0;
    for (const auto &nd : G->getNodes()) {
        if (!nd)
            continue;

        if (nd->getType() == PSNodeType::NOOP) {
            // this should not break the iterator
            removeNode(G, nd.get());
            ++removed;
        }
    }
    return removed;
}

} // namespace pta
} // namespace dg
