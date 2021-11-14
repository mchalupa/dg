#ifndef DG_LLVM_VALUE_RELATIONS_GRAPH_BUILDER_HPP_
#define DG_LLVM_VALUE_RELATIONS_GRAPH_BUILDER_HPP_

#include "llvm/IR/Constants.h"
#include <llvm/IR/CFG.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

#include "GraphElements.h"

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace dg {
namespace vr {

struct GraphBuilder {
    const llvm::Module &module;
    unsigned last_node_id = 0;

    // VRLocation corresponding to the state of the program BEFORE executing the
    // instruction
    std::map<const llvm::Instruction *, VRLocation *> &locationMapping;
    std::map<const llvm::BasicBlock *, std::unique_ptr<VRBBlock>> &blockMapping;

    GraphBuilder(
            const llvm::Module &m,
            std::map<const llvm::Instruction *, VRLocation *> &locs,
            std::map<const llvm::BasicBlock *, std::unique_ptr<VRBBlock>> &blcs)
            : module(m), locationMapping(locs), blockMapping(blcs) {}

    void build() {
        for (const llvm::Function &f : module) {
            build(f);
        }
    }

    void build(const llvm::Function &function) {
        for (const llvm::BasicBlock &block : function) {
            assert(block.size() != 0);
            build(block);
        }

        for (const llvm::BasicBlock &block : function) {
            VRBBlock *vrblock = getVRBBlock(&block);
            assert(vrblock);

            const llvm::Instruction *terminator = block.getTerminator();
            if (llvm::isa<llvm::BranchInst>(terminator)) {
                buildBranch(llvm::cast<llvm::BranchInst>(terminator), vrblock);

            } else if (llvm::isa<llvm::SwitchInst>(terminator)) {
                buildSwitch(llvm::cast<llvm::SwitchInst>(terminator), vrblock);

            } else if (llvm::isa<llvm::ReturnInst>(terminator)) {
                buildReturn(llvm::cast<llvm::ReturnInst>(terminator), vrblock);

            } else if (llvm::succ_begin(&block) != llvm::succ_end(&block)) {
#ifndef NDEBUG
                std::cerr << "Unhandled  terminator: " << std::endl;
                llvm::errs() << "Unhandled terminator: " << *terminator << "\n";
#endif
                abort();
            }
        }
    }

    void buildSwitch(const llvm::SwitchInst *swtch, VRBBlock *vrblock) {
        for (auto &it : swtch->cases()) {
            VRBBlock *succ = getVRBBlock(it.getCaseSuccessor());
            assert(succ);

            auto op = std::unique_ptr<VROp>(new VRAssumeEqual(
                    swtch->getCondition(), it.getCaseValue()));
            vrblock->last()->connect(succ->first(), std::move(op));
        }

        VRBBlock *succ = getVRBBlock(swtch->getDefaultDest());
        assert(succ);
        auto op = std::unique_ptr<VROp>(new VRNoop());
        vrblock->last()->connect(succ->first(), std::move(op));
    }

    void buildBranch(const llvm::BranchInst *inst, VRBBlock *vrblock) {
        if (inst->isUnconditional()) {
            VRBBlock *succ = getVRBBlock(inst->getSuccessor(0));
            assert(succ);

            auto op = std::unique_ptr<VROp>(new VRNoop());

            vrblock->last()->connect(succ->first(), std::move(op));
        } else {
            VRBBlock *trueSucc = getVRBBlock(inst->getSuccessor(0));
            VRBBlock *falseSucc = getVRBBlock(inst->getSuccessor(1));

            auto trueOp = std::unique_ptr<VROp>(
                    new VRAssumeBool(inst->getCondition(), true));
            auto falseOp = std::unique_ptr<VROp>(
                    new VRAssumeBool(inst->getCondition(), false));

            vrblock->last()->connect(trueSucc->first(), std::move(trueOp));
            vrblock->last()->connect(falseSucc->first(), std::move(falseOp));
        }
    }

    void buildReturn(const llvm::ReturnInst *inst, VRBBlock *vrblock) {
        auto op = std::unique_ptr<VROp>(new VRInstruction(inst));

        vrblock->last()->connect(nullptr, std::move(op));
    }

    void build(const llvm::BasicBlock &block) {
        VRBBlock *vrblock = newBBlock(&block);

        auto it = block.begin();
        const llvm::Instruction *previous = &(*it);
        vrblock->append(newLocation(previous));
        ++it;

        for (; it != block.end(); ++it) {
            const llvm::Instruction &inst = *it;
            VRLocation *newLoc = newLocation(&inst);

            vrblock->last()->connect(
                    newLoc, std::unique_ptr<VROp>(new VRInstruction(previous)));

            vrblock->append(newLoc);
            previous = &inst;
        }
    }

    VRLocation *newLocation(const llvm::Instruction *inst) {
        assert(inst);
        assert(locationMapping.find(inst) == locationMapping.end());

        auto location = new VRLocation(++last_node_id);
        assert(location);

        locationMapping.emplace(inst, location);
        return location;
    }

    VRBBlock *newBBlock(const llvm::BasicBlock *B) {
        assert(B);
        assert(blockMapping.find(B) == blockMapping.end());

        auto block = new VRBBlock();
        assert(block);

        blockMapping.emplace(B, std::unique_ptr<VRBBlock>(block));
        return block;
    }

    VRBBlock *getVRBBlock(const llvm::BasicBlock *B) {
        const GraphBuilder *constThis = this;
        return const_cast<VRBBlock *>(constThis->getVRBBlock(B));
    }

    const VRBBlock *getVRBBlock(const llvm::BasicBlock *B) const {
        auto it = blockMapping.find(B);
        return it == blockMapping.end() ? nullptr : it->second.get();
    }

    VRLocation *getVRLocation(const llvm::Instruction *inst) {
        const GraphBuilder *constThis = this;
        return const_cast<VRLocation *>(constThis->getVRLocation(inst));
    }

    const VRLocation *getVRLocation(const llvm::Instruction *inst) const {
        auto it = locationMapping.find(inst);
        return it == locationMapping.end() ? nullptr : it->second;
    }
};

struct GB {
    const llvm::Module &module;
    VRCodeGraph &codeGraph;

    std::map<const llvm::BasicBlock *, VRLocation *> fronts;
    std::map<const llvm::BasicBlock *, VRLocation *> backs;

    void build() {
        for (const llvm::Function &function : module) {
            if (function.isDeclaration())
                continue;

            buildBlocks(function);
            buildTerminators(function);
        }
    }

    void buildBlocks(const llvm::Function &function) {
        for (const llvm::BasicBlock &block : function) {
            assert(block.size() != 0);
            buildBlock(block);
        }

        codeGraph.setEntryLocation(
                &function,
                codeGraph.getVRLocation(&function.getEntryBlock().front()));
    }

    void buildTerminators(const llvm::Function &function) {
        for (const llvm::BasicBlock &block : function) {
            assert(backs.find(&block) != backs.end());
            VRLocation &last = *backs[&block];

            const llvm::Instruction *terminator = block.getTerminator();
            if (auto branch = llvm::dyn_cast<llvm::BranchInst>(terminator)) {
                buildBranch(branch, last);

            } else if (auto swtch =
                               llvm::dyn_cast<llvm::SwitchInst>(terminator)) {
                buildSwitch(swtch, last);

            } else if (auto rturn =
                               llvm::dyn_cast<llvm::ReturnInst>(terminator)) {
                buildReturn(rturn, last);

            } else if (llvm::succ_begin(&block) != llvm::succ_end(&block)) {
#ifndef NDEBUG
                std::cerr << "Unhandled  terminator: "
                          << dg::debug::getValName(terminator) << std::endl;
                llvm::errs() << "Unhandled terminator: " << *terminator << "\n";
#endif
                abort();
            }
        }
    }

    void buildBranch(const llvm::BranchInst *inst, VRLocation &last) {
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

    void buildSwitch(const llvm::SwitchInst *swtch, VRLocation &last) {
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

    void buildReturn(const llvm::ReturnInst *inst, VRLocation &last) {
        last.connect(nullptr, new VRInstruction(inst));
    }

    void buildBlock(const llvm::BasicBlock &block) {
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
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_GRAPH_BUILDER_HPP_
