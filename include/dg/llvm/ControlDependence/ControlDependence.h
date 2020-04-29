#ifndef LLVM_DG_CDA_H_
#define LLVM_DG_CDA_H_

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/ControlDependence/LLVMControlDependenceAnalysisOptions.h"

namespace dg {
//namespace cda {

class LLVMControlDependenceAnalysis {

    const llvm::Module *_module;
    const LLVMControlDependenceAnalysisOptions _options;

public:
    LLVMControlDependenceAnalysis(const llvm::Module *module,
                                  const LLVMControlDependenceAnalysisOptions& opts)
        : _module(module), _options(opts) {}

    using ValVec = std::vector<llvm::Value *>;

    // public API
    const llvm::Module *getModule() const { return _module; }
    const LLVMControlDependenceAnalysisOptions& getOptions() const { return _options; }

    virtual void run();

    virtual ValVec getDependencies(const llvm::Value *v) {
        assert(false && "Not implemented");
        abort();
    }

    virtual ValVec getDependencies(const llvm::BasicBlock *b) {
        assert(false && "Not implemented");
        abort();
    }
};

//} // namespace cda
} // namespace dg

#endif
