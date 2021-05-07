#ifndef DG_LLVM_POINTER_ANALYSIS_OPTIONS_H_
#define DG_LLVM_POINTER_ANALYSIS_OPTIONS_H_

#include "dg/PointerAnalysis/PointerAnalysisOptions.h"
#include "dg/llvm/LLVMAnalysisOptions.h"

namespace dg {

struct LLVMPointerAnalysisOptions : public LLVMAnalysisOptions,
                                    PointerAnalysisOptions {
    enum class AnalysisType { fi, fs, inv, svf } analysisType{AnalysisType::fi};

    bool threads{false};

    bool isFS() const { return analysisType == AnalysisType::fs; }
    bool isFSInv() const { return analysisType == AnalysisType::inv; }
    bool isFI() const { return analysisType == AnalysisType::fi; }
    bool isSVF() const { return analysisType == AnalysisType::svf; }
};

} // namespace dg

#endif
