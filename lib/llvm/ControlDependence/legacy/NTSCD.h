#ifndef DG_LEGACY_LLVM_NTSCD_H_
#define DG_LEGACY_LLVM_NTSCD_H_

#include <llvm/IR/Module.h>

#include "GraphBuilder.h"
#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "dg/util/debug.h"

#include <map>
#include <set>
#include <unordered_map>

#include "Block.h"

namespace llvm {
class Function;
}

namespace dg {

class LLVMPointerAnalysis;

namespace llvmdg {
namespace legacy {

class NTSCD : public LLVMControlDependenceAnalysisImpl {
  public:
    using ValVec = LLVMControlDependenceAnalysis::ValVec;

    struct NodeInfo {
        bool visited = false;
        bool red = false;
        size_t outDegreeCounter = 0;
    };

    NTSCD(const llvm::Module *module,
          const LLVMControlDependenceAnalysisOptions &opts = {},
          LLVMPointerAnalysis *pointsToAnalysis = nullptr);

    void dump(std::ostream &ostream) const;
    void dumpDependencies(std::ostream &ostream) const;

    const std::map<Block *, std::set<Block *>> &controlDependencies() const {
        return controlDependency;
    }

    /// Getters of dependencies for a value
    ValVec getDependencies(const llvm::Instruction * /*unused*/) override {
        return {};
    }
    ValVec getDependent(const llvm::Instruction * /*unused*/) override {
        return {};
    }

    /// Getters of dependencies for a basic block
    ValVec getDependencies(const llvm::BasicBlock *b) override {
        if (_computed.insert(b->getParent()).second) {
            /// FIXME: get rid of the const cast
            computeOnDemand(const_cast<llvm::Function *>(b->getParent()));
        }

        const auto *block = graphBuilder.mapBlock(b);
        if (!block) {
            return {};
        }
        auto it = revControlDependency.find(block->front());
        if (it == revControlDependency.end())
            return {};

        std::set<llvm::Value *> ret;
        for (auto *dep : it->second) {
            assert(dep->llvmBlock() && "Do not have LLVM block");
            ret.insert(const_cast<llvm::BasicBlock *>(dep->llvmBlock()));
        }

        return ValVec{ret.begin(), ret.end()};
    }

    ValVec getDependent(const llvm::BasicBlock * /*unused*/) override {
        assert(false && "Not supported");
        abort();
    }

    // We run on demand. However, you may use manually computeDependencies()
    // to compute all dependencies in the interprocedural CFG.
    void compute(const llvm::Function *F = nullptr) override {
        DBG(cda, "Triggering computation of all dependencies");
        if (F && !F->isDeclaration() && _computed.insert(F).second) {
            computeOnDemand(const_cast<llvm::Function *>(F));
        } else {
            for (const auto &f : *getModule()) {
                if (!f.isDeclaration() && _computed.insert(&f).second) {
                    computeOnDemand(const_cast<llvm::Function *>(&f));
                }
            }
        }
    }

    // Compute dependencies for the whole ICFG (used in legacy code)
    void computeDependencies();

  private:
    GraphBuilder graphBuilder;

    // forward edges (from branchings to dependent blocks)
    std::map<Block *, std::set<Block *>> controlDependency;
    // reverse edges (from dependent blocks to branchings)
    std::map<Block *, std::set<Block *>> revControlDependency;
    std::unordered_map<Block *, NodeInfo> nodeInfo;
    std::set<const llvm::Function *> _computed; // for on-demand

    void computeDependencies(Function * /*function*/);
    void computeOnDemand(llvm::Function *F);

    void computeInterprocDependencies(Function *function);
    void computeIntraprocDependencies(Function *function);

    // a is CD on b
    void addControlDependence(Block *a, Block *b);

    void visitInitialNode(Block *node);
    void visit(Block *node);

    bool hasRedAndNonRedSuccessor(Block *node);
};

} // namespace legacy
} // namespace llvmdg
} // namespace dg

#endif
