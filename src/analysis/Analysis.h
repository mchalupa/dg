#ifndef _DG_ANALYSIS_H_
#define _DG_ANALYSIS_H_

namespace dg {

// forward declaration of BBlock
#ifdef ENABLE_CFG
template <typename NodeT>
class BBlock;
#endif // ENABLE_CFG

namespace analysis {

/// --------------------------------------------------------
//  - Analyses using nodes
/// --------------------------------------------------------
// data for analyses, stored in nodes
struct AnalysesAuxiliaryData
{
    AnalysesAuxiliaryData()
        : lastwalkid(0), dfsorder(0) {}

    // last id of walk (DFS/BFS) that ran on this node
    // ~~> marker if it has been processed
    unsigned int lastwalkid;

    // DFS order number of the node
    unsigned int dfsorder;
};

template <typename NodeT>
class Analysis
{
public:
    AnalysesAuxiliaryData& getAnalysisData(NodeT *n)
    {
        return n->analysisAuxData;
    }
};


#ifdef ENABLE_CFG
/// --------------------------------------------------------
//  - BBlocks analysis
/// --------------------------------------------------------
template <typename NodeT>
class BBlockAnalysis : public Analysis<BBlock<NodeT>>
{
public:
    typedef BBlock<NodeT> *BBlockPtrT;

    AnalysesAuxiliaryData& getAnalysisData(BBlockPtrT BB)
    {
        return BB->analysisAuxData;
    }
};

#endif // ENABLE_CFG

} // namespace analysis
} // namespace dg
#endif //  _DG_ANALYSIS_H_
