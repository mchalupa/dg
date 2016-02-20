#ifndef _LLVM_DG_PSS_H_
#define _LLVM_DG_PSS_H_

#include <map>
#include <llvm/Support/CFG.h>

namespace dg {
namespace analysis {

class PSS;
class PSSNode;

std::pair<PSSNode *, PSSNode *> buildPSSBlock(const llvm::BasicBlock& block,
                                              const llvm::DataLayout *DL);


static void blockAddSuccessors(std::map<const llvm::BasicBlock *,
                                        std::pair<PSSNode *, PSSNode *>>& build_blocks,
                               std::pair<PSSNode *, PSSNode *>& pssn,
                               const llvm::BasicBlock& block)
{
    for (llvm::succ_const_iterator
         S = llvm::succ_begin(&block), SE = llvm::succ_end(&block); S != SE; ++S) {
        std::pair<PSSNode *, PSSNode *>& succ = build_blocks[*S];
        assert(succ.first && succ.second || (!succ.first && !succ.second));
        if (!succ.first) {
            // if we don't have this block built (there was no points-to
            // relevant instruction), we must pretend to be there for
            // control flow information. Thus instead of adding it as
            // successor, add its successors as successors
            blockAddSuccessors(build_blocks, pssn, *(*S));
        } else {
            // add successor to the last nodes
            pssn.second->addSuccessor(succ.first);
        }
    }
}

// build pointer state subgraph for given graph
template <typename PTType>
PSS *buildLLVMPSS(const llvm::Function& F, const llvm::DataLayout *DL)
{
    // here we'll keep first and last nodes of every built block and
    // connected together according to successors
    std::map<const llvm::BasicBlock *, std::pair<PSSNode *, PSSNode *>> build_blocks;
    PSSNode *root = nullptr;
    for (const llvm::BasicBlock& block : F) {
        std::pair<PSSNode *, PSSNode *> nds = buildPSSBlock(block, DL);
        build_blocks[&block] = nds;

        if (!root)
            root = nds.first;
    }

    assert(root && "Did not build anything");
    assert(root == build_blocks[&F.getEntryBlock()].first);

    for (const llvm::BasicBlock& block : F) {
        std::pair<PSSNode *, PSSNode *>& pssn = build_blocks[&block];
        // if the block do not contain any points-to relevant instruction,
        // we returned (nullptr, nullptr)
        // FIXME: do not store such blocks at all
        assert(pssn.first && pssn.second || (!pssn.first && !pssn.second));
        if (!pssn.first)
            continue;

        blockAddSuccessors(build_blocks, pssn, block);
    }

    return new PTType(root);
}

} // namespace dg
} // namespace analysis

#endif
