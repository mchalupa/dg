#ifndef _DG_ANALYSIS_H_
#define _DG_ANALYSIS_H_

namespace dg {
namespace analysis {

/// --------------------------------------------------------
//  - Analyses using nodes
/// --------------------------------------------------------
// data for analyses, stored in nodes
struct AnalysesAuxiliaryData
{
    AnalysesAuxiliaryData() : lastwalkid(0) {}

    // last id of walk (DFS/BFS) that ran on this node
    // ~~> marker if it has been processed
    unsigned int lastwalkid;
};

template <typename NodePtrT>
class Analysis
{
public:
    AnalysesAuxiliaryData& getAnalysisData(NodePtrT n)
    {
        return n->analysisAuxData;
    }
};


#ifdef ENABLE_CFG
template <typename NodePtrT>
class BBlock;

/// --------------------------------------------------------
//  - BBlocks analysis
/// --------------------------------------------------------
template <typename NodePtrT>
class BBlockAnalysis : public Analysis<BBlock<NodePtrT> *>
{
public:
    typedef BBlock<NodePtrT> *BBlockPtrT;

    AnalysesAuxiliaryData& getAnalysisData(BBlockPtrT BB)
    {
        return BB->analysisAuxData;
    }
};

#endif // ENABLE_CFG

} // namespace analysis
} // namespace dg
#endif //  _DG_ANALYSIS_H_
