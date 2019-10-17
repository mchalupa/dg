#ifndef _DG_REACHING_DEFINITIONS_ANALYSIS_H_
#define _DG_REACHING_DEFINITIONS_ANALYSIS_H_

#include <vector>
#include <cassert>

#include "dg/analysis/Offset.h"

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

} // namespace analysis
} // namespace dg

#endif //  _DG_REACHING_DEFINITIONS_ANALYSIS_H_
