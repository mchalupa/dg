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

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_os_ostream.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/analysis/PointsTo/PointerGraph.h"

namespace dg {
namespace analysis {
namespace pta {

void LLVMPointerGraphBuilder::addPHIOperands(const llvm::Function &F)
{
    for (const llvm::BasicBlock& B : F) {
        for (const llvm::Instruction& I : B) {
            if (const llvm::PHINode *PHI = llvm::dyn_cast<llvm::PHINode>(&I)) {
                if (PSNode *node = getNode(PHI))
                    addPHIOperands(node, PHI);
            }
        }
    }
}

// return first and last nodes of the block
PSNodesSeq
LLVMPointerGraphBuilder::buildPointerGraphBlock(const llvm::BasicBlock& block,
                                                PointerSubgraph *parent)
{
    PSNodesSeq blk{nullptr, nullptr};
    for (const llvm::Instruction& Inst : block) {
        if (!isRelevantInstruction(Inst)) {
            // check if it is a zeroing of memory,
            // if so, set the corresponding memory to zeroed
            if (llvm::isa<llvm::MemSetInst>(&Inst))
                checkMemSet(&Inst);

            continue;
        }

        assert(nodes_map.count(&Inst) == 0);

        PSNodesSeq seq = buildInstruction(Inst);
        assert(seq.first &&
               (seq.second || seq.first->getType() == PSNodeType::CALL)
               && "Didn't created the instruction properly");

        // set parent to the new nodes
        PSNode *cur = seq.first;
        while (cur) {
            cur->setParent(parent);
            cur = cur->getSingleSuccessorOrNull();
        }

        if (!seq.second) {
            // the call instruction does not return.
            // Stop building the block here.
            assert(seq.first->getType() == PSNodeType::CALL);
            break;
        }

        // update the return value
        if (blk.first == nullptr)
            blk.first = seq.first;
        blk.second = seq.second;
    }

    return blk;
}

} // namespace pta
} // namespace analysis
} // namespace dg
