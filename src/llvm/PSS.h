#ifndef _LLVM_DG_PSS_H_
#define _LLVM_DG_PSS_H_

#include <map>
#include <llvm/Support/raw_os_ostream.h>

namespace dg {
namespace analysis {

class PSS;
class PSSNode;


// build pointer state subgraph for given graph
// \return   root node of the graph
PSSNode *buildLLVMPSS(const llvm::Function& F, const llvm::DataLayout *DL);

std::pair<PSSNode *, PSSNode *> buildPSSBlock(const llvm::BasicBlock& block,
                                              const llvm::DataLayout *DL);

std::pair<PSSNode *, PSSNode *> buildGlobals(const llvm::Module *M,
                                             const llvm::DataLayout *DL);

template <typename PTType>
PSS *buildLLVMPSS(const llvm::Module *M, const llvm::DataLayout *DL)
{
    // get entry function
    llvm::Function *F = M->getFunction("main");
    if (!F) {
        llvm::errs() << "Need main function in module\n";
        abort();
    }

    // first we must build globals, because nodes can use them as operands
    std::pair<PSSNode *, PSSNode *> glob = buildGlobals(M, DL);

    // now we can build rest of the graph
    PSSNode *root = buildLLVMPSS(*F, DL);

    // do we have any globals at all? If so, insert them at the begining of the graph
    // FIXME: we do not need to process them later, should we do it somehow differently?
    // something like 'static nodes' in PSS...
    if (glob.first) {
        assert(glob.second && "Have the start but not the end");

        // this is a sequence of global nodes, make it the root of the graph
        glob.second->addSuccessor(root);
        root = glob.first;
    }

    return new PTType(root);
}


} // namespace dg
} // namespace analysis

#endif
