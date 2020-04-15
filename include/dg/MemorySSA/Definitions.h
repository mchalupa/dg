#ifndef DG_MEMORY_SSA_DEFINITIONS_H_
#define DG_MEMORY_SSA_DEFINITIONS_H_

#include <vector>
#include <set>

#include "dg/Offset.h"
#include "dg/MemorySSA/DefinitionsMap.h"
#include "dg/ReadWriteGraph/RWNode.h"

namespace dg {
namespace dda {

// information about definitions associated to each bblock
struct Definitions {
    bool _processed{false};

    // definitions gathered at the end of this bblock
    // (if you find the sought memory here,
    // you got all definitions from this block)
    DefinitionsMap<RWNode> definitions;
    // all memory that is overwritten by this block (strong update)
    // FIXME: we should have just a mapping from memory to disjunctive intervals
    // as data structure here (if you find the sought memory here, you can
    // terminate the search)
    DefinitionsMap<RWNode> kills;

    // writes to unknown memory in this block
    std::vector<RWNode*> unknownWrites;
    // just a cache
    std::vector<RWNode*> unknownReads;

    void swap(Definitions& rhs) {
        definitions.swap(rhs.definitions);
        kills.swap(rhs.kills);
        unknownWrites.swap(rhs.unknownWrites);
        unknownReads.swap(rhs.unknownReads);
    }

    void addUnknownWrite(RWNode *n) {
        unknownWrites.push_back(n);
    }

    void addUnknownRead(RWNode *n) {
        unknownReads.push_back(n);
    }

    const std::vector<RWNode *>& getUnknownWrites() const {
        return unknownWrites;
    }

    const std::vector<RWNode *>& getUnknownReads() const {
        return unknownReads;
    }

    ///
    /// get the definition-sites for the given 'ds'
    ///
    std::set<RWNode *> get(const DefSite& ds) {
        auto retval = definitions.get(ds);
        if (retval.empty()) {
            retval.insert(unknownWrites.begin(), unknownWrites.end());
        }
        return retval;
    }

    // update this Definitions by definitions from 'node'.
    // I.e., as if node would be executed when already
    // having the definitions we have
    void update(RWNode *node, RWNode *defnode = nullptr);
    // Join another definitions to this Definitions
    // (as if on  a joint point in a CFG)
    void join(const Definitions& rhs);

    auto uncovered(const DefSite& ds) const -> decltype(kills.undefinedIntervals(ds)) {
        return kills.undefinedIntervals(ds);
    }

    // for on-demand analysis
    // once isProcessed is true, the Defiitions contain
    // summarized all the information that one needs
    // (may be then altered just only by adding a phi node definitions)
    bool isProcessed() const { return _processed; }
    void setProcessed() { _processed = true; }

#ifndef NDEBUG
    void dump() const;
#endif
};

} // namespace dda
} // namespace dg

#endif
