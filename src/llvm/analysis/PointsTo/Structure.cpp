#include <cassert>
#include <set>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Config/llvm-config.h>

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
 #include <llvm/Support/CFG.h>
#else
 #include <llvm/IR/CFG.h>
#endif

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Constant.h>
#include <llvm/Support/raw_os_ostream.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "analysis/PointsTo/PointerSubgraph.h"
#include "PointerSubgraph.h"

namespace dg {
namespace analysis {
namespace pta {

static size_t blockAddSuccessors(std::map<const llvm::BasicBlock *,
                                          PSNodesSeq>& built_blocks,
                                 std::set<const llvm::BasicBlock *>& found_blocks,
                                 PSNodesSeq& ptan,
                                 const llvm::BasicBlock& block)
{
    size_t num = 0;

    for (llvm::succ_const_iterator
         S = llvm::succ_begin(&block), SE = llvm::succ_end(&block); S != SE; ++S) {

         // we already processed this block? Then don't try to add the edges again
         if (!found_blocks.insert(*S).second)
            continue;

        PSNodesSeq& succ = built_blocks[*S];
        assert((succ.first && succ.second) || (!succ.first && !succ.second));
        if (!succ.first) {
            // if we don't have this block built (there was no points-to
            // relevant instruction), we must pretend to be there for
            // control flow information. Thus instead of adding it as
            // successor, add its successors as successors
            num += blockAddSuccessors(built_blocks, found_blocks, ptan, *(*S));
        } else {
            // add successor to the last nodes
            ptan.second->addSuccessor(succ.first);
            ++num;
        }

        // assert that we didn't corrupt the block
        assert((succ.first && succ.second) || (!succ.first && !succ.second));
    }

    return num;
}

PSNodesSeq
LLVMPointerSubgraphBuilder::buildArguments(const llvm::Function& F)
{
    PSNodesSeq seq;
    PSNode *last = nullptr;

    int idx = 0;
    for (auto A = F.arg_begin(), E = F.arg_end(); A != E; ++A, ++idx) {
        auto it = nodes_map.find(&*A);
        if (it == nodes_map.end())
            continue;

        PSNodesSeq& cur = it->second;
        assert(cur.first == cur.second);

        if (!seq.first) {
            assert(!last);
            seq.first = cur.first;
        } else {
            assert(last);
            last->addSuccessor(cur.first);
        }

        last = cur.second;
    }

    seq.second = last;

    assert((seq.first && seq.second) || (!seq.first && !seq.second));

    return seq;
}

PSNodesSeq LLVMPointerSubgraphBuilder::buildBlockStructure(const llvm::BasicBlock& block)
{
    PSNodesSeq seq = PSNodesSeq(nullptr, nullptr);

    PSNode *last = nullptr;
    for (const llvm::Instruction& Inst : block) {
        auto it = nodes_map.find(&Inst);
        if (it == nodes_map.end()) {
            assert(!isRelevantInstruction(Inst));
            continue;
        }

        PSNodesSeq& cur = it->second;

        if (!seq.first) {
            assert(!last);
            seq.first = cur.first;
        } else {
            assert(last);
            last->addSuccessor(cur.first);
        }

        // We store only the call node
        // in the nodes_map, so there is not valid (call, return)
        // sequence but only one node (actually, for call that may not
        // be a sequence). We need to "insert" whole call here,
        // so set the return node as the last node
        if (llvm::isa<llvm::CallInst>(&Inst) &&
            // undeclared funcs do not have paired nodes
            cur.first->getPairedNode()) {
            last = cur.first->getPairedNode();
        } else
            last = cur.second;
    }

    seq.second = last;

    assert((seq.first && seq.second) || (!seq.first && !seq.second));

    if (seq.first)
        built_blocks[&block] = seq;

    return seq;
}

void LLVMPointerSubgraphBuilder::addProgramStructure(const llvm::Function *F,
                                                     Subgraph& subg)
{
    assert(subg.root && "Subgraph has no root");
    assert(subg.ret && "Subgraph has no ret");

    // with function pointer calls it may happen that we try
    // to add structure more times, so bail out in that case
    if (subg.has_structure)
        return;

    PSNodesSeq args = buildArguments(*F);
    PSNode *lastNode = nullptr;

    // make arguments the entry block of the subgraphs (if there
    // are any arguments)
    if (args.first) {
        assert(args.second && "BUG: Have only first argument");
        subg.root->addSuccessor(args.first);

        // inset the variadic arg node into the graph if needed
        if (F->isVarArg()) {
            assert(subg.vararg);
            args.second->addSuccessor(subg.vararg);
            lastNode = subg.vararg;
        } else
            lastNode = args.second;
    } else if (subg.vararg) {
        // this function has only ... argument
        assert(F->isVarArg());
        assert(!args.second && "BUG: Have only last argument");
        subg.root->addSuccessor(subg.vararg);
        lastNode = subg.vararg;
    } else {
        assert(!args.second && "BUG: Have only last argument");
        lastNode = subg.root;
    }

    assert(lastNode);

    // add successors in one basic block
    for (const llvm::BasicBlock* block : subg.llvmBlocks)
        buildBlockStructure(*block);

    // check whether we create the entry block. If not, we would
    // have a problem while adding successors, so fake that
    // the entry block is the root or the last argument
    const llvm::BasicBlock *entry = &F->getBasicBlockList().front();
    PSNodesSeq& enblk = built_blocks[entry];
    if (!enblk.first) {
        assert(!enblk.second);
        enblk.first = subg.root;
        enblk.second = lastNode;
    } else {
        // if we have the entry block, just make it the successor
        // of the root or the last argument
        lastNode->addSuccessor(enblk.first);
    }

    std::vector<PSNode *> rets;
    for (const llvm::BasicBlock& block : *F) {
        PSNodesSeq& ptan = built_blocks[&block];
        // if the block does not contain any points-to relevant instruction,
        // we get (nullptr, nullptr)
        assert((ptan.first && ptan.second) || (!ptan.first && !ptan.second));
        if (!ptan.first)
            continue;

        // add successors to this block (skipping the empty blocks).
        // To avoid infinite loops we use found_blocks container that will
        // server as a mark in BFS/DFS - the program should not contain
        // so many blocks that this could have some big overhead. If proven
        // otherwise later, we'll change this.
        std::set<const llvm::BasicBlock *> found_blocks;
        size_t succ_num = blockAddSuccessors(built_blocks,
                                             found_blocks,
                                             ptan, block);

        // if we have not added any successor, then the last node
        // of this block is a return node
        if (succ_num == 0 && ptan.second->getType() == PSNodeType::RETURN)
            rets.push_back(ptan.second);

        assert(ptan.first && ptan.second);
    }

    // add successors edges from every real return to our artificial ret node
    // NOTE: if the function has infinite loop we won't have any return nodes,
    // so this assertion must not hold
    //assert(!rets.empty() && "BUG: Did not find any return node in function");
    for (PSNode *r : rets) {
        r->addSuccessor(subg.ret);
    }

    // set parents of nodes
    // FIXME: we should do this when creating the nodes
    std::set<PSNode *> cont;
    getNodes(cont, subg.root, subg.ret, 0xdead);
    for (PSNode* n : cont) {
        n->setParent(subg.root);
    }

    subg.has_structure = true;
}

} // namespace pta
} // namespace analysis
} // namespace dg
