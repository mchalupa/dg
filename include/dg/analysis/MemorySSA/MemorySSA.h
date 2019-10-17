#ifndef DG_MEMORY_SSA_H_
#define DG_MEMORY_SSA_H_

#include <vector>
#include <set>
#include <cassert>

#include "dg/analysis/Offset.h"

#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitionsAnalysisOptions.h"
#include "dg/analysis/ReachingDefinitions/DefinitionsMap.h"

#include "dg/analysis/ReadWriteGraph/ReadWriteGraph.h"

#include "dg/util/debug.h"

namespace dg {
namespace analysis {

class MemorySSATransformation : public ReachingDefinitionsAnalysis {
    void performLvn();
    void performLvn(RWBBlock *block);
    void performGvn();

    ////
    // LVN
    ///

    // Find definitions of the def site and return def-use edges.
    // For the (possibly) uncovered bytes create phi nodes (which are also returned
    // as the definitions) in _this very block_. It is important for LVN.
    std::vector<RWNode *> findDefinitionsInBlock(RWBBlock *, const DefSite&);

    ////
    // GVN
    ///
    // Find definitions of the def site and return def-use edges.
    // For the uncovered bytes create phi nodes (which are also returned
    // as the definitions).
    std::vector<RWNode *> findDefinitions(RWBBlock *, const DefSite&);

    /// Finding definitions for unknown memory
    // Must be called after LVN proceeded - ideally only when the client is getting the definitions
    std::vector<RWNode *> findAllReachingDefinitions(RWNode *from);
    void findAllReachingDefinitions(DefinitionsMap<RWNode>& defs, RWBBlock *from,
                                    std::set<RWNode *>& nodes,
                                    std::set<RWBBlock *>& visitedBlocks);

    // all phi nodes added during transformation to SSA
    std::vector<RWNode *> _phis;

public:
    MemorySSATransformation(ReadWriteGraph&& graph,
                                   const ReachingDefinitionsAnalysisOptions& opts)
    : ReachingDefinitionsAnalysis(std::move(graph), opts) {}

    MemorySSATransformation(ReadWriteGraph&& graph)
    : ReachingDefinitionsAnalysis(std::move(graph)) {}

    void run() override {
        DBG_SECTION_BEGIN(dda, "Running MemorySSA analysis");
        // transform the graph to SSA
        if (graph.getBBlocks().empty())
            graph.buildBBlocks();

        performLvn();
        performGvn();

        DBG_SECTION_END(dda, "Running MemorySSA analysis finished");
    }

    // return the reaching definitions of ('mem', 'off', 'len')
    // at the location 'where'
    std::vector<RWNode *>
    getReachingDefinitions(RWNode *, RWNode *,
                           const Offset&,
                           const Offset&) override {
        assert(false && "This method is not implemented for this analysis");
        abort();
    }

    std::vector<RWNode *> getReachingDefinitions(RWNode *use) override;
};

} // namespace analysis
} // namespace dg

#endif // DG_MEMORY_SSA_H_
