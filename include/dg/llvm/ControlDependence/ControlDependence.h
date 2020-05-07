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

class LLVMControlDependenceAnalysisImpl {

    const llvm::Module *_module;
    const LLVMControlDependenceAnalysisOptions _options;

public:
    LLVMControlDependenceAnalysisImpl(const llvm::Module *module,
                                      const LLVMControlDependenceAnalysisOptions& opts)
        : _module(module), _options(opts) {}

    virtual ~LLVMControlDependenceAnalysisImpl() = default;

    using ValVec = std::vector<llvm::Value *>;

    // public API
    const llvm::Module *getModule() const { return _module; }
    const LLVMControlDependenceAnalysisOptions& getOptions() const { return _options; }

    virtual void run() = 0;

    /// Getters of dependencies for a value
    virtual ValVec getDependencies(const llvm::Value *) = 0;
    virtual ValVec getDependent(const llvm::Value *) = 0;

    /// Getters of dependencies for a basic block
    virtual ValVec getDependencies(const llvm::BasicBlock *) = 0;
    virtual ValVec getDependent(const llvm::BasicBlock *) = 0;
};


class LLVMControlDependenceAnalysis {

    const llvm::Module *_module;
    const LLVMControlDependenceAnalysisOptions _options;
    std::unique_ptr<LLVMControlDependenceAnalysisImpl> _impl{nullptr};

    void initializeImpl();

public:
    LLVMControlDependenceAnalysis(const llvm::Module *module,
                                  const LLVMControlDependenceAnalysisOptions& opts)
        : _module(module), _options(opts) {
        initializeImpl();
    }

    using ValVec = std::vector<llvm::Value *>;

    // public API
    const llvm::Module *getModule() const { return _module; }
    const LLVMControlDependenceAnalysisOptions& getOptions() const { return _options; }

    void run() { _impl->run(); }

    ValVec getDependencies(const llvm::Value *v) {
        return _impl->getDependencies(v);
    }

    ValVec getDependencies(const llvm::BasicBlock *b) {
        return _impl->getDependencies(b);
    }

    ValVec getDependent(const llvm::Value *v) {
        return _impl->getDependent(v);
    }

    ValVec getDependent(const llvm::BasicBlock *b) {
        return _impl->getDependent(b);
    }

    // FIXME: add also API that return just iterators
};

//} // namespace cda
} // namespace dg

#endif
