#ifndef _LLVM_DG_CONTROL_EXPRESSION_H_
#define _LLVM_DG_CONTROL_EXPRESSION_H_

#include <cassert>
#include <llvm/IR/Module.h>

#include <llvm/Config/llvm-config.h>
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
 #include <llvm/Support/CFG.h>
#else
 #include <llvm/IR/CFG.h>
#endif

#include "dg/analysis/ControlExpression/CFA.h"
#include "dg/analysis/ControlExpression/ControlExpression.h"

namespace dg {

using LLVMCFA = CFA<llvm::BasicBlock *>;
using LLVMCFANode = CFANode<llvm::BasicBlock *>;

class LLVMCFABuilder {

public:
    LLVMCFA build(llvm::Function& F)
    {
        std::map<llvm::BasicBlock *, LLVMCFANode *> mapping;
        LLVMCFA cfa;

        // create nodes for all basic blocks
        for (llvm::BasicBlock& B : F) {
            mapping[&B] = new LLVMCFANode(&B);
        }

        // add successors for all basic blocks
        for (llvm::BasicBlock& B : F) {
            LLVMCFANode *node = mapping[&B];
            assert(node);

            // iterate over all successors of the basic block
            for (llvm::succ_iterator
                 S = succ_begin(&B), E = succ_end(&B); S != E; ++S) {
                LLVMCFANode *succ = mapping[*S];

                // add the successor
                node->addSuccessor(succ);
            }

            // add the node into CFA -- we
            // must do it now, after the node is fully
            // initialized (when it has successors)
            cfa.addNode(node);
        }

        return cfa;
    }

};


} // namespace dg

#endif // _LLVM_DG_CONTROL_EXPRESSION_H_
