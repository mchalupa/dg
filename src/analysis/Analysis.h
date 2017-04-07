#ifndef _DG_ANALYSIS_H_
#define _DG_ANALYSIS_H_

namespace dg {

// forward declaration of BBlock
#ifdef ENABLE_CFG
template <typename NodeT>
class BBlock;
#endif // ENABLE_CFG

namespace analysis {

// data for analyses, stored in nodes
struct AnalysesAuxiliaryData
{
    AnalysesAuxiliaryData()
        : lastwalkid(0), dfsorder(0), bfsorder(0) {}

    // last id of walk (DFS/BFS) that ran on this node
    // ~~> marker if it has been processed
    unsigned int lastwalkid;

    // DFS order number of the node
    unsigned int dfsorder;
    // BFS order number of the node
    unsigned int bfsorder;
};

// gather statistics about a run
struct AnalysisStatistics
{
    AnalysisStatistics()
        : processedBlocks(0), processedNodes(0) {};

    uint64_t processedBlocks;
    uint64_t processedNodes;

    uint64_t getProcessedBlocks() const { return processedBlocks; }
    uint64_t getProcessedNodes() const { return processedNodes; }
};

/// --------------------------------------------------------
//  - Analyses using nodes
/// --------------------------------------------------------
template <typename NodeT>
class Analysis
{
public:
    AnalysesAuxiliaryData& getAnalysisData(NodeT *n)
    {
        return n->analysisAuxData;
    }

    const AnalysisStatistics& getStatistics() const
    {
        return statistics;
    }

protected:
    AnalysisStatistics statistics;
};


#ifdef ENABLE_CFG
/// --------------------------------------------------------
//  - BBlocks analysis
/// --------------------------------------------------------
template <typename NodeT>
class BBlockAnalysis : public Analysis<BBlock<NodeT>>
{
public:
    using BBlockPtrT = BBlock<NodeT> *;

    AnalysesAuxiliaryData& getAnalysisData(BBlockPtrT BB)
    {
        return BB->analysisAuxData;
    }
};

#endif // ENABLE_CFG

} // namespace analysis
} // namespace dg
#endif //  _DG_ANALYSIS_H_
