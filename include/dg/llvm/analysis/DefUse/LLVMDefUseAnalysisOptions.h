#ifndef _DG_LLVM_DEF_USE_ANALYSIS_OPTIONS_H_
#define _DG_LLVM_DEF_USE_ANALYSIS_OPTIONS_H_

#include "dg/llvm/analysis/LLVMAnalysisOptions.h"
#include "dg/analysis/DefUse/DefUseAnalysisOptions.h"

namespace dg {
namespace analysis {

struct LLVMDefUseAnalysisOptions : public LLVMAnalysisOptions, DefUseAnalysisOptions
{
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_DEF_USE_ANALYSIS_OPTIONS_H_
