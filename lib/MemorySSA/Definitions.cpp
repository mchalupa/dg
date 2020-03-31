#include "dg/MemorySSA/MemorySSA.h"

#include "dg/util/debug.h"

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
void
MemorySSATransformation::Definitions::update(RWNode *node, RWNode *defnode) {
    if (!defnode)
        defnode = node;

    // possible definitions
    for (auto& ds : node->getDefines()) {
        if (ds.target->isUnknown()) {
            // this makes all lastDefs into possibleDefs,
            // since we do not know if it was defined here or there
            // also add the definition as a proper target for Gvn
            definitions.addAll(defnode);
            addUnknownWrite(defnode);
        } else {
            definitions.add(ds, defnode);
        }
    }

    // definitive definitions
    for (auto& ds : node->getOverwrites()) {
        assert((defnode->getType() == RWNodeType::PHI || // we allow ? for PHI nodes
               !ds.offset.isUnknown()) && "Update on unknown offset");
        assert(!ds.target->isUnknown() && "Update on unknown memory");

        kills.add(ds, defnode);
        definitions.update(ds, defnode);
    }

    // gather unknown uses
    if (node->usesUnknown()) {
        addUnknownRead(defnode);
    }
}

void
MemorySSATransformation::Definitions::join(const Definitions& rhs) {
    definitions.add(rhs.definitions);
    kills = kills.intersect(rhs.kills);
    unknownWrites.insert(unknownWrites.end(),
                        rhs.unknownWrites.begin(), rhs.unknownWrites.end());
    unknownReads.insert(unknownReads.end(),
                        rhs.unknownReads.begin(), rhs.unknownReads.end());
}

} // namespace dda
} // namespace dg
