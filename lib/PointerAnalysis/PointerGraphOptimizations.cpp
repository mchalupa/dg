#include "dg/PointerAnalysis/PointerGraph.h"
#include "dg/PointerAnalysis/PointerGraphOptimizations.h"

namespace dg {
namespace pta {

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

void PSUnknownsReducer::processAllocs() {
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
                    user->removeAllOperands();
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
                user->removeAllOperands();
                PS->remove(user);
                ++removed;
            }

            nd->isolate();
            nd->removeAllOperands();
            PS->remove(nd.get());
            assert(nd.get() == nullptr);
            ++removed;
        }
    }
}


} // namespace pta
} // namespace dg
