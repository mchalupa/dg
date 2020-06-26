#ifndef LLVM_DG_CDA_IMPL_H_
#define LLVM_DG_CDA_IMPL_H_

#include "dg/llvm/ControlDependence/LLVMControlDependenceAnalysisOptions.h"

namespace llvm {
    class Module;
    class Value;
    class Function;
};

namespace dg {
//namespace cda {
namespace llvmdg {
 class CDGraph;
}

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

    virtual llvmdg::CDGraph *getGraph(const llvm::Function *) { return nullptr; }
    virtual const llvmdg::CDGraph *getGraph(const llvm::Function *) const { return nullptr; }

    virtual void run() = 0;

    /// Getters of dependencies for a value
    virtual ValVec getDependencies(const llvm::Instruction *) = 0;
    virtual ValVec getDependent(const llvm::Instruction *) = 0;

    /// Getters of dependencies for a basic block
    virtual ValVec getDependencies(const llvm::BasicBlock *) = 0;
    virtual ValVec getDependent(const llvm::BasicBlock *) = 0;

    /// Getter for noreturn nodes in function (for interprocedural analysis)
    virtual ValVec getNoReturns(const llvm::Function *) const {
        assert(false && "Unsupported"); abort();
    }
};


//} // namespace cda
} // namespace dg

#endif
