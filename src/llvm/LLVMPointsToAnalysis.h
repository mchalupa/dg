#ifndef _LLVM_DG_POINTS_TO_ANALYSIS_H_
#define _LLVM_DG_POINTS_TO_ANALYSIS_H_

#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>

#include "analysis/PSS.h"
#include "llvm/PSS.h"

namespace dg {
namespace analysis {
    class PSS;
}

template <typename PTType>
class LLVMPointsToAnalysis {
    analysis::PSS *pss;
    // starting function
    const llvm::Function *func;
    const llvm::DataLayout *DL;
    const llvm::Module *module;

public:
    LLVMPointsToAnalysis(const llvm::Function* F)
    : func(F)
    {
        module = F->getParent();
        DL = new llvm::DataLayout(module->getDataLayout());
    }

    LLVMPointsToAnalysis(const llvm::Module* M)
    : module(M)
    {
        func = M->getFunction("main");
        assert(func && "Need main function");

        DL = new llvm::DataLayout(module->getDataLayout());
    }

    void run()
    {
        pss = analysis::buildLLVMPSS<PTType>(*func, DL);
        pss->run();
    }

    analysis::PSS *getPSS() const { return pss; }
};

}

#endif // _LLVM_DG_POINTS_TO_ANALYSIS_H_
