#ifndef LLVM_DG_CDA_H_
#define LLVM_DG_CDA_H_

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_os_ostream.h>

#include <utility>

#include "dg/llvm/ControlDependence/LLVMControlDependenceAnalysisImpl.h"
#include "dg/llvm/ControlDependence/LLVMControlDependenceAnalysisOptions.h"

namespace dg {
// namespace cda {

class LLVMPointerAnalysis;

namespace llvmdg {
class CallGraph;
}

class LLVMControlDependenceAnalysis {
  public:
    using ValVec = std::vector<llvm::Value *>;

  private:
    const llvm::Module *_module;
    const LLVMControlDependenceAnalysisOptions _options;
    std::unique_ptr<LLVMControlDependenceAnalysisImpl> _impl{nullptr};
    std::unique_ptr<LLVMControlDependenceAnalysisImpl> _interprocImpl{nullptr};

    void initializeImpl(LLVMPointerAnalysis *pta = nullptr,
                        llvmdg::CallGraph *cg = nullptr);

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
                                  LLVMControlDependenceAnalysisOptions opts,
                                  LLVMPointerAnalysis *pta = nullptr)
            : _module(module), _options(std::move(opts)) {
        initializeImpl(pta);
    }

    // public API
    const llvm::Module *getModule() const { return _module; }
    const LLVMControlDependenceAnalysisOptions &getOptions() const {
        return _options;
    }

    LLVMControlDependenceAnalysisImpl *getImpl() { return _impl.get(); }
    const LLVMControlDependenceAnalysisImpl *getImpl() const {
        return _impl.get();
    }

    // Compute control dependencies for all functions.
    // If the analysis works on demand, calling this method
    // will trigger the computation for the given function
    // or the whole module if the function is nullptr.
    // (so you don't want to call that if you want
    //  on demand)
    void compute(const llvm::Function *F = nullptr) {
        _impl->compute(F);
        if (getOptions().interproceduralCD())
            _interprocImpl->compute(F);
    }

    ValVec getDependencies(const llvm::Instruction *v) {
        return _getDependencies(v);
    }
    ValVec getDependent(const llvm::Instruction *v) { return _getDependent(v); }

    ValVec getDependencies(const llvm::BasicBlock *b) {
        return _getDependencies(b);
    }
    ValVec getDependent(const llvm::BasicBlock *b) { return _getDependent(b); }

    ValVec getNoReturns(const llvm::Function *F) {
        if (_interprocImpl)
            return _interprocImpl->getNoReturns(F);
        return {};
    }

    // A getter for results of closure-based algorithms.
    // The method may abort if used with non-closure-based analysis.
    ValVec getClosure(const llvm::Function *F,
                      const std::set<llvm::Value *> &vals) {
        return _impl->getClosure(F, vals);
    }
    /// XXX TBD
    //// A getter for iterative building of results of closure-based algorithms.
    // void startClosure(const llvm::Function *F, const std::set<llvm::Value *>&
    // startSet) {
    //    return _impl->startClosure(F, startSet);
    //}
    //// A getter for iterative building of results of closure-based algorithms.
    // void closeSet(const std::set<llvm::Value *>& nodesset) {
    //    return _impl->closeSet(nodesset);
    //}

    // FIXME: add also API that return just iterators
};

//} // namespace cda
} // namespace dg

#endif
