#ifndef DG_LLVM_NTSCD_H_
#define DG_LLVM_NTSCD_H_

#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "GraphBuilder.h"

#include <set>
#include <map>
#include <unordered_map>


namespace llvm {
class Function;
}

namespace dg {

class LLVMPointerAnalysis;

namespace llvmdg {

class NTSCD : public LLVMControlDependenceAnalysisImpl {
public:
    using ValVec = LLVMControlDependenceAnalysis::ValVec;

    struct NodeInfo {
        bool visited = false;
        bool red = false;
        size_t outDegreeCounter = 0;
    };

    NTSCD(const llvm::Module *module,
          const LLVMControlDependenceAnalysisOptions& opts = {},
          LLVMPointerAnalysis *pointsToAnalysis = nullptr)
        : LLVMControlDependenceAnalysisImpl(module, opts), graphBuilder(module) {}

    /// Getters of dependencies for a value
    ValVec getDependencies(const llvm::Instruction *) override { return {}; }
    ValVec getDependent(const llvm::Instruction *) override { return {}; }

    /// Getters of dependencies for a basic block
    ValVec getDependencies(const llvm::BasicBlock *b) override {
       //if (_computed.insert(b->getParent()).second) {
       //    /// FIXME: get rid of the const cast
       //    computeOnDemand(const_cast<llvm::Function*>(b->getParent()));
       //}

       //auto *block = graphBuilder.mapBlock(b);
       //if (!block) {
       //    return {};
       //}
       //auto it = revControlDependency.find(block->front());
       //if (it == revControlDependency.end())
       //    return {};

       //std::set<llvm::Value *> ret;
       //for (auto *dep : it->second) {
       //    assert(dep->llvmBlock() && "Do not have LLVM block");
       //    ret.insert(const_cast<llvm::BasicBlock*>(dep->llvmBlock()));
       //}

       //return ValVec{ret.begin(), ret.end()};
    }

    ValVec getDependent(const llvm::BasicBlock *) override {
        assert(false && "Not supported");
        abort();
    }

    // We run on demand. However, you may use manually computeDependencies()
    // to compute all dependencies in the interprocedural CFG.
    void run() override { /* we run on demand */ }

private:
    CDGraphBuilder graphBuilder;

   //CDGraph&& build() {
   //    // FIXME: this is a bit of a hack
   //    if (!PTA->getOptions().isSVF()) {
   //        auto dgpta = static_cast<DGLLVMPointerAnalysis *>(PTA);
   //        llvmdg::CallGraph CG(dgpta->getPTA()->getPG()->getCallGraph());
   //        buildFromLLVM(&CG);
   //    } else {
   //        buildFromLLVM();
   //    }
   //}

    // forward edges (from branchings to dependent blocks)
   //std::map<CDBBlock *, std::set<CDBBlock *>> controlDependency;
   //// reverse edges (from dependent blocks to branchings)
   //std::map<CDBBlock *, std::set<CDBBlock *>> revControlDependency;
   //std::unordered_map<CDBBlock *, NodeInfo> nodeInfo;
   //std::set<const llvm::Function *> _computed; // for on-demand

   //void computeDependencies(Function *);
   //void computeOnDemand(llvm::Function *F);

    // a is CD on b
    void addControlDependence(CDBBlock *a, CDBBlock *b);
};

} // namespace llvmdg
} // namespace dg

#endif
