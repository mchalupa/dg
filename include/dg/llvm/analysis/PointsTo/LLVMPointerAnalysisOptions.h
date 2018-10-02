#ifndef _DG_LLVM_POINTER_ANALYSIS_OPTIONS_H_
#define _DG_LLVM_POINTER_ANALYSIS_OPTIONS_H_

#include "dg/llvm/analysis/LLVMAnalysisOptions.h"
#include "dg/analysis/PointsTo/PointerAnalysisOptions.h"

namespace dg {
namespace analysis {

struct LLVMPointerAnalysisOptions : public LLVMAnalysisOptions, PointerAnalysisOptions
{
    enum class AnalysisType { fi, fs, inv } analysisType{AnalysisType::fi};

    bool isFS() const { return analysisType == AnalysisType::fs; }
    bool isFSInv() const { return analysisType == AnalysisType::inv; }
    bool isFI() const { return analysisType == AnalysisType::fi; }
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_POINTER_ANALYSIS_OPTIONS_H_
