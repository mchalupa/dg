#ifndef _LLVM_DG_SLICER_H_
#define _LLVM_DG_SLICER_H_

#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>

#include "analysis/Slicing.h"
#include "LLVMDependenceGraph.h"
#include "LLVMNode.h"

namespace dg {

class LLVMNode;

class LLVMSlicer : public analysis::Slicer<LLVMNode>
{
public:
    /* virtual */
    void removeNode(LLVMNode *node)
    {
        using namespace llvm;

        Value *val = const_cast<Value *>(node->getKey());
        Instruction *Inst = dyn_cast<Instruction>(val);
        if (Inst)
            Inst->eraseFromParent();
    }
};

} // namespace dg

#endif  // _LLVM_DG_SLICER_H_

