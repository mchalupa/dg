#ifndef _DG_LLVM_ANALYSIS_OPTIONS_H_
#define _DG_LLVM_ANALYSIS_OPTIONS_H_

#include <string>
#include "dg/analysis/AnalysisOptions.h"

namespace dg {
namespace analysis {

struct LLVMAnalysisOptions {
    // Number of bytes in objects to track precisely
    std::string entryFunction{"main"};

    LLVMAnalysisOptions& setEntryFunction(const std::string& e) {
        entryFunction = e; return *this;
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_ANALYSIS_OPTIONS_H_
