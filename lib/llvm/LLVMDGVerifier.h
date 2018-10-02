#ifndef _LLVM_DG_VERIFIER_H_
#define _LLVM_DG_VERIFIER_H_

#include "dg/llvm/LLVMDependenceGraph.h"

namespace llvm {
    class Function;
    class BasicBlock;
}

namespace dg {

// verify if the built dg is ok
// this is friend class of LLVMDependenceGraph,
// so we can do everything!
class LLVMDGVerifier {
    const LLVMDependenceGraph *dg;
    unsigned int faults;

    void fault(const char *fmt, ...);
    void checkMainProc();
    void checkGraph(llvm::Function *, LLVMDependenceGraph *);
    void checkBBlock(const llvm::BasicBlock *, LLVMBBlock *);
    void checkNode(const llvm::Value *, LLVMNode *);
public:
    LLVMDGVerifier(const LLVMDependenceGraph *g) : dg(g), faults(0) {}
    bool verify();
};

}

#endif // _LLVM_DG_VERIFIER_H_
