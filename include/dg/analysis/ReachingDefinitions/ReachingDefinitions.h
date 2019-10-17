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
    ReachingDefinitionsGraph graph;

    const ReachingDefinitionsAnalysisOptions options;

public:
    ReachingDefinitionsAnalysis(ReachingDefinitionsGraph&& graph,
                                const ReachingDefinitionsAnalysisOptions& opts)
    : graph(std::move(graph)), options(opts)
    {
        assert(graph.getRoot() && "Root cannot be null");
        // with max_set_size == 0 (everything is defined on unknown location)
        // we get unsound results with vararg functions and similar weird stuff
        assert(options.maxSetSize > 0 && "The set size must be at least 1");
    }

    ReachingDefinitionsAnalysis(ReachingDefinitionsGraph&& graph)
    : ReachingDefinitionsAnalysis(std::move(graph), {}) {}
    virtual ~ReachingDefinitionsAnalysis() = default;

    // get nodes in BFS order and store them into
    // the container
    template <typename ContainerOrNode>
    std::vector<RDNode *> getNodes(const ContainerOrNode& start,
                                   unsigned expected_num = 0) {
        return graph.getNodes(start, expected_num);
    }

    RDNode *getRoot() const { return graph.getRoot(); }
    ReachingDefinitionsGraph *getGraph() { return &graph; }
    const ReachingDefinitionsGraph *getGraph() const { return &graph; }

    bool processNode(RDNode *n);
    virtual void run();

    // return the reaching definitions of ('mem', 'off', 'len')
    // at the location 'where'
    virtual std::vector<RDNode *>
    getReachingDefinitions(RDNode *where, RDNode *mem,
                           const Offset& off,
                           const Offset& len);

    // return reaching definitions of a node that represents
    // the given use
    virtual std::vector<RDNode *> getReachingDefinitions(RDNode *use);
};

class SSAReachingDefinitionsAnalysis : public ReachingDefinitionsAnalysis {
    void performLvn();
    void performLvn(RDBBlock *block);
    void performGvn();

    ////
    // LVN
    ///

    // Find definitions of the def site and return def-use edges.
    // For the (possibly) uncovered bytes create phi nodes (which are also returned
    // as the definitions) in _this very block_. It is important for LVN.
    std::vector<RDNode *> findDefinitionsInBlock(RDBBlock *, const DefSite&);

    ////
    // GVN
    ///
    // Find definitions of the def site and return def-use edges.
    // For the uncovered bytes create phi nodes (which are also returned
    // as the definitions).
    std::vector<RDNode *> findDefinitions(RDBBlock *, const DefSite&);

    /// Finding definitions for unknown memory
    // Must be called after LVN proceeded - ideally only when the client is getting the definitions
    std::vector<RDNode *> findAllReachingDefinitions(RDNode *from);
    void findAllReachingDefinitions(DefinitionsMap<RDNode>& defs, RDBBlock *from,
                                    std::set<RDNode *>& nodes,
                                    std::set<RDBBlock *>& visitedBlocks);

    // all phi nodes added during transformation to SSA
    std::vector<RDNode *> _phis;

public:
    SSAReachingDefinitionsAnalysis(ReachingDefinitionsGraph&& graph,
                                   const ReachingDefinitionsAnalysisOptions& opts)
    : ReachingDefinitionsAnalysis(std::move(graph), opts) {}

    SSAReachingDefinitionsAnalysis(ReachingDefinitionsGraph&& graph)
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
    std::vector<RDNode *>
    getReachingDefinitions(RDNode *, RDNode *,
                           const Offset&,
                           const Offset&) override {
        assert(false && "This method is not implemented for this analysis");
        abort();
    }

    std::vector<RDNode *> getReachingDefinitions(RDNode *use) override;
};

} // namespace analysis
} // namespace dg

#endif //  _DG_REACHING_DEFINITIONS_ANALYSIS_H_
