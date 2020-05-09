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
#include "dg/llvm/ControlDependence/LLVMControlDependenceAnalysisImpl.h"

namespace dg {
//namespace cda {

class LLVMControlDependenceAnalysis {
public:
    using ValVec = std::vector<llvm::Value *>;

private:
    const llvm::Module *_module;
    const LLVMControlDependenceAnalysisOptions _options;
    std::unique_ptr<LLVMControlDependenceAnalysisImpl> _impl{nullptr};
    std::unique_ptr<LLVMControlDependenceAnalysisImpl> _interprocImpl{nullptr};

    void initializeImpl();

    template <typename ValT>
    ValVec _getDependencies(ValT v) {
        assert(_impl);
        auto ret = _impl->getDependencies(v);

        if (getOptions().interproceduralCD()) {
            assert(_interprocImpl);
            auto interproc = _interprocImpl->getDependencies(v);
            ret.insert(ret.end(), interproc.begin(), interproc.end());
        }
        return ret;
    }

    template <typename ValT>
    ValVec _getDependent(ValT v) {
        assert(_impl);
        auto ret = _impl->getDependent(v);

        if (getOptions().interproceduralCD()) {
            assert(_interprocImpl);
            auto interproc = _interprocImpl->getDependent(v);
            ret.insert(ret.end(), interproc.begin(), interproc.end());
        }
        return ret;
    }


public:
    LLVMControlDependenceAnalysis(const llvm::Module *module,
                                  const LLVMControlDependenceAnalysisOptions& opts)
        : _module(module), _options(opts) {
        initializeImpl();
    }

    // public API
    const llvm::Module *getModule() const { return _module; }
    const LLVMControlDependenceAnalysisOptions& getOptions() const { return _options; }

    void run() {
        _impl->run();
        if (getOptions().interproceduralCD())
            _interprocImpl->run();
    }

    ValVec getDependencies(const llvm::Instruction *v) { return _getDependencies(v); }
    ValVec getDependent(const llvm::Instruction *v) { return _getDependent(v); }

    ValVec getDependencies(const llvm::BasicBlock *b) { return _getDependencies(b); }
    ValVec getDependent(const llvm::BasicBlock *b) { return _getDependent(b); }

    ValVec getNoReturns(const llvm::Function *F) const {
        if (_interprocImpl)
            return _interprocImpl->getNoReturns(F);
        return {};
    }

    // FIXME: add also API that return just iterators
};

//} // namespace cda
} // namespace dg

#endif
