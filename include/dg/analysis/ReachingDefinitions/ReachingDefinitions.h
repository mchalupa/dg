#ifndef _DG_REACHING_DEFINITIONS_ANALYSIS_H_
#define _DG_REACHING_DEFINITIONS_ANALYSIS_H_

#include <vector>
#include <list>
#include <set>
#include <cassert>
#include <memory>

#include "dg/analysis/Offset.h"
#include "dg/analysis/BFS.h"

#include "dg/analysis/ReachingDefinitions/ReachingDefinitionsAnalysisOptions.h"
#include "dg/analysis/ReachingDefinitions/DefinitionsMap.h"

#include "dg/analysis/ReadWriteGraph/ReadWriteGraph.h"

#include "dg/util/debug.h"

namespace dg {
namespace analysis {

// here the types are for type-checking (optional - user can do it
// when building the graph) and for later optimizations

class ReachingDefinitionsAnalysis
{
protected:
    ReadWriteGraph graph;

    const ReachingDefinitionsAnalysisOptions options;

public:
    ReachingDefinitionsAnalysis(ReadWriteGraph&& graph,
                                const ReachingDefinitionsAnalysisOptions& opts)
    : graph(std::move(graph)), options(opts)
    {
        assert(graph.getRoot() && "Root cannot be null");
        // with max_set_size == 0 (everything is defined on unknown location)
        // we get unsound results with vararg functions and similar weird stuff
        assert(options.maxSetSize > 0 && "The set size must be at least 1");
    }

    ReachingDefinitionsAnalysis(ReadWriteGraph&& graph)
    : ReachingDefinitionsAnalysis(std::move(graph), {}) {}
    virtual ~ReachingDefinitionsAnalysis() = default;

    // get nodes in BFS order and store them into
    // the container
    template <typename ContainerOrNode>
    std::vector<RWNode *> getNodes(const ContainerOrNode& start,
                                   unsigned expected_num = 0) {
        return graph.getNodes(start, expected_num);
    }

    RWNode *getRoot() const { return graph.getRoot(); }
    ReadWriteGraph *getGraph() { return &graph; }
    const ReadWriteGraph *getGraph() const { return &graph; }

    bool processNode(RWNode *n);
    virtual void run();

    // return the reaching definitions of ('mem', 'off', 'len')
    // at the location 'where'
    virtual std::vector<RWNode *>
    getReachingDefinitions(RWNode *where, RWNode *mem,
                           const Offset& off,
                           const Offset& len);

    // return reaching definitions of a node that represents
    // the given use
    virtual std::vector<RWNode *> getReachingDefinitions(RWNode *use);
};

class SSAReachingDefinitionsAnalysis : public ReachingDefinitionsAnalysis {
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
    SSAReachingDefinitionsAnalysis(ReadWriteGraph&& graph,
                                   const ReachingDefinitionsAnalysisOptions& opts)
    : ReachingDefinitionsAnalysis(std::move(graph), opts) {}

    SSAReachingDefinitionsAnalysis(ReadWriteGraph&& graph)
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

#endif //  _DG_REACHING_DEFINITIONS_ANALYSIS_H_
