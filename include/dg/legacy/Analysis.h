#ifndef DG_LEGACY_ANALYSIS_H_
#define DG_LEGACY_ANALYSIS_H_

namespace dg {
namespace legacy {

// data for analyses, stored in nodes
struct AnalysesAuxiliaryData {
    AnalysesAuxiliaryData() = default;

    // last id of walk (DFS/BFS) that ran on this node
    // ~~> marker if it has been processed
    unsigned int lastwalkid{0};

    // DFS order number of the node
    unsigned int dfsorder{0};
    // BFS order number of the node
    unsigned int bfsorder{0};
};

// gather statistics about a run
struct AnalysisStatistics {
    AnalysisStatistics() = default;

    uint64_t processedBlocks{0};
    uint64_t processedNodes{0};

    uint64_t getProcessedBlocks() const { return processedBlocks; }
    uint64_t getProcessedNodes() const { return processedNodes; }
};

/// --------------------------------------------------------
//  - Analyses using nodes
/// --------------------------------------------------------
template <typename NodeT>
class Analysis {
  public:
    AnalysesAuxiliaryData &getAnalysisData(NodeT *n) {
        return n->analysisAuxData;
    }

    const AnalysisStatistics &getStatistics() const { return statistics; }

  protected:
    AnalysisStatistics statistics;
};

} // namespace legacy
} // namespace dg

namespace dg {

// forward declaration of BBlock
template <typename NodeT>
class BBlock;

namespace legacy {

/// --------------------------------------------------------
//  - BBlocks analysis
/// --------------------------------------------------------
template <typename NodeT>
class BBlockAnalysis : public Analysis<BBlock<NodeT>> {
  public:
    using BBlockPtrT = BBlock<NodeT> *;

    AnalysesAuxiliaryData &getAnalysisData(BBlockPtrT BB) {
        return BB->analysisAuxData;
    }
};

} // namespace legacy
} // namespace dg

#endif
