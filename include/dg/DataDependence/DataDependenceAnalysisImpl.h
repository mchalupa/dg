#ifndef DG_DATA_DEPENDENCE_ANALYSIS_IMPL_H_
#define DG_DATA_DEPENDENCE_ANALYSIS_IMPL_H_

#include <cassert>
#include <utility>

#include "dg/DataDependence/DataDependenceAnalysisOptions.h"
#include "dg/Offset.h"
#include "dg/ReadWriteGraph/ReadWriteGraph.h"

#include "dg/util/debug.h"

namespace dg {
namespace dda {

// here the types are for type-checking (optional - user can do it
// when building the graph) and for later optimizations

class DataDependenceAnalysisImpl {
  protected:
    ReadWriteGraph graph;

    const DataDependenceAnalysisOptions options;

  public:
    DataDependenceAnalysisImpl(ReadWriteGraph &&graph,
                               DataDependenceAnalysisOptions opts)
            : graph(std::move(graph)), options(std::move(opts)) {
        assert(this->graph.getEntry() && "Graph has no entry");
    }

    DataDependenceAnalysisImpl(ReadWriteGraph &&graph)
            : DataDependenceAnalysisImpl(std::move(graph), {}) {}

    virtual ~DataDependenceAnalysisImpl() = default;

    ReadWriteGraph *getGraph() { return &graph; }
    const ReadWriteGraph *getGraph() const { return &graph; }
    const RWSubgraph *getEntry() const { return graph.getEntry(); }
    // FIXME: rename to getEntryNode();
    // RWNode *getRoot() { return graph.getEntry()->getRoot(); }
    const RWNode *getRoot() const { return graph.getEntry()->getRoot(); }

    virtual void run() = 0;

    // return the reaching definitions of ('mem', 'off', 'len')
    // at the location 'where'
    virtual std::vector<RWNode *> getDefinitions(RWNode *where, RWNode *mem,
                                                 const Offset &off,
                                                 const Offset &len) = 0;

    // return reaching definitions of a node that represents
    // the given use
    virtual std::vector<RWNode *> getDefinitions(RWNode *use) = 0;
};

} // namespace dda
} // namespace dg

#endif // DG_DATA_DEPENDENCE_ANALYSIS_IMPL_H_
