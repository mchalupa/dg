#ifndef DG_LLVM_VALUE_RELATIONS_GRAPH_BUILDER_H_
#define DG_LLVM_VALUE_RELATIONS_GRAPH_BUILDER_H_

#include "GraphElements.h"

#include <llvm/IR/Module.h>

namespace dg {
namespace vr {

class GraphBuilder {
    const llvm::Module &module;
    VRCodeGraph &codeGraph;

    std::map<const llvm::BasicBlock *, VRLocation *> fronts;
    std::map<const llvm::BasicBlock *, VRLocation *> backs;

    void buildBlocks(const llvm::Function &function);

    void buildTerminators(const llvm::Function &function);

    void buildBranch(const llvm::BranchInst *inst, VRLocation &last);

    void buildSwitch(const llvm::SwitchInst *swtch, VRLocation &last);

    static void buildReturn(const llvm::ReturnInst *inst, VRLocation &last);

    void buildBlock(const llvm::BasicBlock &block);

  public:
    GraphBuilder(const llvm::Module &m, VRCodeGraph &c)
            : module(m), codeGraph(c) {}

    void build();
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_GRAPH_BUILDER_H_
