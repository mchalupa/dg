#ifndef _DG_LLVM_REACHING_DEFINITIONS_ANALYSIS_OPTIONS_H_
#define _DG_LLVM_REACHING_DEFINITIONS_ANALYSIS_OPTIONS_H_

#include "dg/llvm/analysis/LLVMAnalysisOptions.h"
#include "dg/analysis/ReachingDefinitions/ReachingDefinitionsAnalysisOptions.h"

namespace dg {
namespace analysis {

struct LLVMReachingDefinitionsAnalysisOptions :
    public LLVMAnalysisOptions, ReachingDefinitionsAnalysisOptions
{
    // FIXME: rename ss to sparse
    enum class AnalysisType { dense, ss } analysisType{AnalysisType::dense};

    bool threads;
    bool isDense() const { return analysisType == AnalysisType::dense; }
    bool isSparse() const { return analysisType == AnalysisType::ss; }
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_REACHING_DEFINITIONS_ANALYSIS_OPTIONS_H_
