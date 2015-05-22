#ifndef _DG_ANALYSIS_H_
#define _DG_ANALYSIS_H_

namespace dg {
namespace analysis {

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

} // namespace analysis
} // namespace dg
#endif //  _DG_ANALYSIS_H_
