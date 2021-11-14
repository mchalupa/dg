#include "dg/llvm/ValueRelations/GraphBuilder.h"

#include <llvm/IR/CFG.h>

#ifndef NDEBUG
#include "dg/llvm/ValueRelations/getValName.h"
#endif

namespace dg {
namespace vr {

void GraphBuilder::build() {
    for (const llvm::Function &function : module) {
        if (function.isDeclaration())
            continue;

        buildBlocks(function);
        buildTerminators(function);
    }
}

void GraphBuilder::buildBlocks(const llvm::Function &function) {
    for (const llvm::BasicBlock &block : function) {
        assert(!block.empty());
        buildBlock(block);
    }

    codeGraph.setEntryLocation(
            &function,
            codeGraph.getVRLocation(&function.getEntryBlock().front()));
}

void GraphBuilder::buildTerminators(const llvm::Function &function) {
    for (const llvm::BasicBlock &block : function) {
        assert(backs.find(&block) != backs.end());
        VRLocation &last = *backs[&block];

        const llvm::Instruction *terminator = block.getTerminator();
        if (const auto *branch = llvm::dyn_cast<llvm::BranchInst>(terminator)) {
            buildBranch(branch, last);

        } else if (const auto *swtch =
                           llvm::dyn_cast<llvm::SwitchInst>(terminator)) {
            buildSwitch(swtch, last);

        } else if (const auto *rturn =
                           llvm::dyn_cast<llvm::ReturnInst>(terminator)) {
            buildReturn(rturn, last);

        } else if (llvm::succ_begin(&block) != llvm::succ_end(&block)) {
#ifndef NDEBUG
            std::cerr << "Unhandled  terminator: "
                      << dg::debug::getValName(terminator) << "\n";
            llvm::errs() << "Unhandled terminator: " << *terminator << "\n";
#endif
            abort();
        }
    }
}

void GraphBuilder::buildBranch(const llvm::BranchInst *inst, VRLocation &last) {
    if (inst->isUnconditional()) {
        assert(fronts.find(inst->getSuccessor(0)) != fronts.end());
        VRLocation &first = *fronts[inst->getSuccessor(0)];

        last.connect(first, new VRNoop());
    } else {
        VRLocation &truePadding = codeGraph.newVRLocation();
        VRLocation &falsePadding = codeGraph.newVRLocation();

        auto *condition = inst->getCondition();
        last.connect(truePadding, new VRAssumeBool(condition, true));
        last.connect(falsePadding, new VRAssumeBool(condition, false));

        assert(fronts.find(inst->getSuccessor(0)) != fronts.end());
        assert(fronts.find(inst->getSuccessor(1)) != fronts.end());

        VRLocation &firstTrue = *fronts[inst->getSuccessor(0)];
        VRLocation &firstFalse = *fronts[inst->getSuccessor(1)];

        truePadding.connect(firstTrue, new VRNoop());
        falsePadding.connect(firstFalse, new VRNoop());
    }
}

void GraphBuilder::buildSwitch(const llvm::SwitchInst *swtch,
                               VRLocation &last) {
    for (auto &it : swtch->cases()) {
        VRLocation &padding = codeGraph.newVRLocation();

        last.connect(padding, new VRAssumeEqual(swtch->getCondition(),
                                                it.getCaseValue()));

        assert(fronts.find(it.getCaseSuccessor()) != fronts.end());
        VRLocation &first = *fronts[it.getCaseSuccessor()];

        padding.connect(first, new VRNoop());
    }

    assert(fronts.find(swtch->getDefaultDest()) != fronts.end());
    VRLocation &first = *fronts[swtch->getDefaultDest()];
    last.connect(first, new VRNoop());
}

void GraphBuilder::buildReturn(const llvm::ReturnInst *inst, VRLocation &last) {
    last.connect(nullptr, new VRInstruction(inst));
}

void GraphBuilder::buildBlock(const llvm::BasicBlock &block) {
    auto it = block.begin();
    const llvm::Instruction *previousInst = &(*it);
    VRLocation *previousLoc = &codeGraph.newVRLocation(previousInst);
    ++it;

    fronts.emplace(&block, previousLoc);

    for (; it != block.end(); ++it) {
        const llvm::Instruction &inst = *it;
        VRLocation &newLoc = codeGraph.newVRLocation(&inst);

        previousLoc->connect(newLoc, new VRInstruction(previousInst));

        previousInst = &inst;
        previousLoc = &newLoc;
    }

    backs.emplace(&block, previousLoc);
}

} // namespace vr
} // namespace dg
