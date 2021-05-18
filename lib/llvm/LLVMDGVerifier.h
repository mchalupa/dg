#ifndef LLVM_DG_VERIFIER_H_
#define LLVM_DG_VERIFIER_H_

#include "dg/llvm/LLVMDependenceGraph.h"

namespace llvm {
class Function;
class BasicBlock;
} // namespace llvm

namespace dg {

// verify if the built dg is ok
// this is friend class of LLVMDependenceGraph,
// so we can do everything!
class LLVMDGVerifier {
    const LLVMDependenceGraph *dg;
    unsigned int faults;

    void fault(const char *fmt, ...);
    void checkMainProc();
    void checkGraph(llvm::Function * /*F*/, LLVMDependenceGraph * /*g*/);
    void checkBBlock(const llvm::BasicBlock * /*llvmBB*/, LLVMBBlock * /*BB*/);
    void checkNode(const llvm::Value * /*val*/, LLVMNode * /*node*/);

  public:
    LLVMDGVerifier(const LLVMDependenceGraph *g) : dg(g), faults(0) {}
    bool verify();
};

} // namespace dg

#endif // LLVM_DG_VERIFIER_H_
