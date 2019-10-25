#ifndef DG_REACHING_DEFINITIONS_ANALYSIS_H_
#define DG_REACHING_DEFINITIONS_ANALYSIS_H_

#include <vector>
#include <cassert>

#include "dg/Offset.h"

#include "dg/DataDependence/DataDependenceAnalysisOptions.h"
#include "dg/DataDependence/DataDependenceAnalysisImpl.h"

#include "dg/ReadWriteGraph/ReadWriteGraph.h"

#include "dg/util/debug.h"

namespace dg {
namespace dda {

// here the types are for type-checking (optional - user can do it
// when building the graph) and for later optimizations

class ReachingDefinitionsAnalysis : public DataDependenceAnalysisImpl {
public:
    ReachingDefinitionsAnalysis(ReadWriteGraph&& graph,
                                const DataDependenceAnalysisOptions& opts)
    : DataDependenceAnalysisImpl(std::move(graph), opts) {
        // with max_set_size == 0 (everything is defined on unknown location)
        // we get unsound results with vararg functions and similar weird stuff
        assert(options.maxSetSize > 0 && "The set size must be at least 1");
    }

    ReachingDefinitionsAnalysis(ReadWriteGraph&& graph)
    : DataDependenceAnalysisImpl(std::move(graph)) {}

    bool processNode(RWNode *n);

    void run() override;

    // return the reaching definitions of ('mem', 'off', 'len')
    // at the location 'where'
    std::vector<RWNode *> getDefinitions(RWNode *where, RWNode *mem,
                                         const Offset& off,
                                         const Offset& len) override;

    // return reaching definitions of a node that represents
    // the given use
    std::vector<RWNode *> getDefinitions(RWNode *use) override;
};

} // namespace dda
} // namespace dg

#endif //  _DG_REACHING_DEFINITIONS_ANALYSIS_H_
