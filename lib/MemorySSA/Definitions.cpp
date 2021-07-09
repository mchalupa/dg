#include "dg/MemorySSA/MemorySSA.h"

#include "dg/util/debug.h"

#ifndef NDEBUG
#include <iostream>
#endif

namespace dg {
namespace dda {

/// ------------------------------------------------------------------
// class Definitions
/// ------------------------------------------------------------------
///
// Update Definitions object with definitions from 'node'.
// 'defnode' is the node that should be added as the
// node that performs these definitions. Usually, defnode == node
// (defnode was added to correctly handle call nodes where the
// definitions are stored separately, but we want to have the call node
// as the node that defines the memory.
///
void Definitions::update(RWNode *node, RWNode *defnode) {
    if (!defnode)
        defnode = node;

    // possible definitions
    for (const auto &ds : node->getDefines()) {
        if (ds.target->isUnknown()) {
            // add the definition to every known set of definitions
            // that we have so far
            definitions.addAll(defnode);
            // add the definition also as a proper target for Gvn,
            // since the previous step is not enough
            addUnknownWrite(defnode);
        } else {
            definitions.add(ds, defnode);
        }
    }

    // definitive definitions
    for (const auto &ds : node->getOverwrites()) {
        assert((defnode->isPhi() || // we allow ? for PHI nodes
                !ds.offset.isUnknown()) &&
               "Update on unknown offset");
        assert(!ds.target->isUnknown() && "Update on unknown memory");

        kills.add(ds, defnode);
        definitions.update(ds, defnode);
    }
}

void Definitions::join(const Definitions &rhs) {
    definitions.add(rhs.definitions);
    kills = kills.intersect(rhs.kills);
    unknownWrites.insert(unknownWrites.end(), rhs.unknownWrites.begin(),
                         rhs.unknownWrites.end());
}

#ifndef NDEBUG
void Definitions::dump() const {
    std::cout << "processed: " << _processed << "\n";
    std::cout << " -- defines -- \n";
    definitions.dump();
    std::cout << " -- kills -- \n";
    kills.dump();
    std::cout << "\n";
    std::cout << " -- unknown writes -- \n";
    for (auto *nd : unknownWrites) {
        std::cout << nd->getID() << " ";
    }
    std::cout << std::endl;
}
#endif

} // namespace dda
} // namespace dg
