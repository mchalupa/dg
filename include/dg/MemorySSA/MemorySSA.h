#ifndef DG_MEMORY_SSA_H_
#define DG_MEMORY_SSA_H_

#include <vector>
#include <set>
#include <cassert>
#include <unordered_map>

#include "dg/Offset.h"

#include "dg/DataDependence/DataDependenceAnalysisOptions.h"
#include "dg/DataDependence/DataDependenceAnalysisImpl.h"
#include "dg/MemorySSA/DefinitionsMap.h"

#include "dg/ReadWriteGraph/ReadWriteGraph.h"

#include "dg/ADT/Queue.h"
#include "dg/util/debug.h"

namespace dg {
namespace dda {

class MemorySSATransformation : public DataDependenceAnalysisImpl {
    //void performLvn();
    void performLvn(RWSubgraph *);
    void performLvn(RWBBlock *);

    //void performGvn();
    void performGvn(RWSubgraph *);

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
        // cache for all definitions that reach the end of this block.
        // You can terminate the search once you find this map and it is
        // non-empty.
        DefinitionsMap<RWNode> allDefinitions;

        // writes to unknown memory in this block
        std::vector<RWNode*> unknownWrites;
        // just a cache
        std::vector<RWNode*> unknownReads;

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

        void update(RWNode *node, RWNode *defnode = nullptr);

        auto uncovered(const DefSite& ds) const -> decltype(kills.undefinedIntervals(ds)) {
            return kills.undefinedIntervals(ds);
        }

        // for on-demand analysis
        bool isProcessed() const { return _processed; }
        void setProcessed() { _processed = true; }
    };

    ////
    // LVN
    ///
    // Perform LVN up to a certain point.
    // XXX: we could avoid this by (at least virtually) splitting blocks on uses.
    Definitions findDefinitionsInBlock(RWNode *);

    ////
    // GVN
    ///
    // Find definitions of the def site and return def-use edges.
    // For the uncovered bytes create phi nodes (which are also returned
    // as the definitions).
    std::vector<RWNode *> findDefinitions(RWBBlock *, const DefSite&);

    ////
    // GVN
    //
    // Find definitions for the given node (which is supposed to be a use)
    std::vector<RWNode *> findDefinitions(RWNode *node);

    std::vector<RWNode *> findDefinitionsInPredecessors(RWBBlock *block,
                                                        const DefSite& ds);

    void findPhiDefinitions(RWNode *phi);

    /// Finding definitions for unknown memory
    // Must be called after LVN proceeded - ideally only when the client is getting the definitions
    std::vector<RWNode *> findAllReachingDefinitions(RWNode *from);
    void findAllReachingDefinitions(DefinitionsMap<RWNode>& defs,
                                    RWBBlock *from,
                                    std::set<RWBBlock *>& visitedBlocks);

    void updateCallDefinitions(Definitions& D, RWNodeCall *call);
    void updateDefinitions(Definitions& D, RWNode *node);

    // insert a (temporary) use into the graph before the node 'where'
    RWNode *insertUse(RWNode *where, RWNode *mem,
                      const Offset& off, const Offset& len);

    /// compute a summary for a function
    Definitions *computeSummary(RWSubgraph *);

    std::vector<RWNode *> _phis;
    dg::ADT::QueueLIFO<RWNode> _queue;
    std::unordered_map<RWBBlock *, Definitions> _defs;
    // summaries for subgraphs
    std::unordered_map<RWSubgraph *, Definitions> _summaries;

public:
    MemorySSATransformation(ReadWriteGraph&& graph,
                            const DataDependenceAnalysisOptions& opts)
    : DataDependenceAnalysisImpl(std::move(graph), opts) {}

    MemorySSATransformation(ReadWriteGraph&& graph)
    : DataDependenceAnalysisImpl(std::move(graph)) {}

    void run() override;

    // return the reaching definitions of ('mem', 'off', 'len')
    // at the location 'where'
    std::vector<RWNode *> getDefinitions(RWNode *where,
                                         RWNode *mem,
                                         const Offset& off,
                                         const Offset& len) override;

    std::vector<RWNode *> getDefinitions(RWNode *use) override;

    const Definitions *getBBlockDefinitions(RWBBlock *b) const {
        auto it = _defs.find(b);
        if (it == _defs.end())
            return nullptr;
        return &it->second;
    }

    const Definitions *getSummary(RWSubgraph *s) const {
        auto it = _summaries.find(s);
        if (it == _summaries.end())
            return nullptr;
        return &it->second;
    }
};

} // namespace dda
} // namespace dg

#endif // DG_MEMORY_SSA_H_
