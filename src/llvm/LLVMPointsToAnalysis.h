#ifndef _LLVM_DG_POINTS_TO_ANALYSIS_H_
#define _LLVM_DG_POINTS_TO_ANALYSIS_H_

#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>

#include "analysis/PSS.h"
#include "llvm/PSS.h"

namespace dg {
namespace analysis {
namespace pss {
    class PSS;
}
}

using analysis::pss::PSS;
using analysis::pss::LLVMPSSBuilder;

template <typename PTType>
class LLVMPointsToAnalysis {
    PSS *pss;
    LLVMPSSBuilder *builder;

public:
    LLVMPointsToAnalysis(const llvm::Module* M)
        :builder(new LLVMPSSBuilder(M)) {}

    void run()
    {
        pss = builder->buildLLVMPSS<PTType>();
        pss->run();
    }

    PSS *getPSS() const { return pss; }
};

}

#endif // _LLVM_DG_POINTS_TO_ANALYSIS_H_
