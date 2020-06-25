#ifndef DG_LLVM_SCD_H_
#define DG_LLVM_SCD_H_

#include "dg/llvm/ControlDependence/ControlDependence.h"

#include <set>
#include <map>
#include <unordered_map>


namespace llvm {
class Function;
}

namespace dg {

class LLVMPointerAnalysis;

namespace llvmdg {

// Standard control dependencies based on the computation
// of post-dominance frontiers.
// This class uses purely LLVM, no internal representation
// like the other classes (we use the post-dominance computation from LLVM).
class SCD : public LLVMControlDependenceAnalysisImpl {

    void computePostDominators(llvm::Function& F);

    std::unordered_map<const llvm::BasicBlock *, std::set<llvm::BasicBlock *>> dependentBlocks;
    std::unordered_map<const llvm::BasicBlock *, std::set<llvm::BasicBlock *>> dependencies;
    std::set<const llvm::Function *> _computed;

public:
    using ValVec = LLVMControlDependenceAnalysis::ValVec;

    SCD(const llvm::Module *module,
        const LLVMControlDependenceAnalysisOptions& opts = {})
        : LLVMControlDependenceAnalysisImpl(module, opts) {}

    /// Getters of dependencies for a value
    ValVec getDependencies(const llvm::Instruction *) override { return {}; }
    ValVec getDependent(const llvm::Instruction *) override { return {}; }

    /// Getters of dependencies for a basic block
    ValVec getDependencies(const llvm::BasicBlock *b) override {
        if (_computed.insert(b->getParent()).second) {
            /// FIXME: get rid of the const cast
            computePostDominators(*const_cast<llvm::Function*>(b->getParent()));
        }

        auto &S = dependencies[b];
        return ValVec{S.begin(), S.end()};
    }

    ValVec getDependent(const llvm::BasicBlock *b) override {
        if (_computed.insert(b->getParent()).second) {
            computePostDominators(*const_cast<llvm::Function*>(b->getParent()));
        }
        auto &S = dependentBlocks[b];
        return ValVec{S.begin(), S.end()};
    }

    void run() override { /* we work on-demand */ }
};

} // namespace llvmdg
} // namespace dg

#endif
