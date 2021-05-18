#ifndef LLVM_DG_CDA_IMPL_H_
#define LLVM_DG_CDA_IMPL_H_

#include <set>
#include <utility>

#include "dg/llvm/ControlDependence/LLVMControlDependenceAnalysisOptions.h"

namespace llvm {
class Module;
class Value;
class Function;
}; // namespace llvm

namespace dg {

class CDGraph;

class LLVMControlDependenceAnalysisImpl {
    const llvm::Module *_module;
    const LLVMControlDependenceAnalysisOptions _options;

  public:
    LLVMControlDependenceAnalysisImpl(const llvm::Module *module,
                                      LLVMControlDependenceAnalysisOptions opts)
            : _module(module), _options(std::move(opts)) {}

    virtual ~LLVMControlDependenceAnalysisImpl() = default;

    using ValVec = std::vector<llvm::Value *>;

    // public API
    const llvm::Module *getModule() const { return _module; }
    const LLVMControlDependenceAnalysisOptions &getOptions() const {
        return _options;
    }

    virtual CDGraph *getGraph(const llvm::Function * /*unused*/) {
        return nullptr;
    }
    virtual const CDGraph *getGraph(const llvm::Function * /*unused*/) const {
        return nullptr;
    }

    // Compute control dependencies for all functions.
    // If the analysis works on demand, calling this method
    // will trigger the computation for the given function
    // or the whole module if the function is nullptr.
    // (so you don't want to call that if you want
    //  on demand)
    virtual void compute(const llvm::Function *F = nullptr) = 0;

    /// Getters of dependencies for a value
    virtual ValVec getDependencies(const llvm::Instruction *) = 0;
    virtual ValVec getDependent(const llvm::Instruction *) = 0;

    /// Getters of dependencies for a basic block
    virtual ValVec getDependencies(const llvm::BasicBlock *) = 0;
    virtual ValVec getDependent(const llvm::BasicBlock *) = 0;

    /// Getter for noreturn nodes in function (for interprocedural analysis)
    virtual ValVec getNoReturns(const llvm::Function * /*unused*/) {
        assert(false && "Unsupported");
        abort();
    }

    virtual ValVec getClosure(const llvm::Function * /*unused*/,
                              const std::set<llvm::Value *> & /*unused*/) {
        assert(false && "Unsupported");
        abort();
    }
};

} // namespace dg

#endif
