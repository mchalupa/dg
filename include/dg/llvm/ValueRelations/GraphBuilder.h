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

    using VRBBlockHandle = VRCodeGraph::VRBBlockHandle;
    using VRBBlock = const VRCodeGraph::VRBBlock &;

    GB(const llvm::Module &m, VRCodeGraph &c) : module(m), codeGraph(c) {}

    void build() {
        for (const llvm::Function &function : module) {
            buildBlocks(function);
            buildTerminators(function);
            setEntry(function);
        }
    }

    void buildBlocks(const llvm::Function &function) {
        for (const llvm::BasicBlock &block : function) {
            assert(block.size() != 0);
            buildBlock(block);
        }
    }

    void buildTerminators(const llvm::Function &function) {
        for (const llvm::BasicBlock &block : function) {
            VRBBlock vrblock = codeGraph.getVRBBlock(&block);

            const llvm::Instruction *terminator = block.getTerminator();
            if (auto branch = llvm::dyn_cast<llvm::BranchInst>(terminator)) {
                buildBranch(branch, vrblock);

            } else if (auto swtch =
                               llvm::dyn_cast<llvm::SwitchInst>(terminator)) {
                buildSwitch(swtch, vrblock);

            } else if (auto rturn =
                               llvm::dyn_cast<llvm::ReturnInst>(terminator)) {
                buildReturn(rturn, vrblock);

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

    void setEntry(const llvm::Function &function) {
        VRBBlock &entryBlock = codeGraph.getVRBBlock(&function.getEntryBlock());
        codeGraph.setEntryLocation(&function, entryBlock.front());
    }

    void buildBranch(const llvm::BranchInst *inst, VRBBlock vrblock) {
        if (inst->isUnconditional()) {
            VRBBlock &succ = codeGraph.getVRBBlock(inst->getSuccessor(0));

            vrblock.back().connect(succ.front(), new VRNoop());
        } else {
            VRBBlock &trueSucc = codeGraph.getVRBBlock(inst->getSuccessor(0));
            VRBBlock &falseSucc = codeGraph.getVRBBlock(inst->getSuccessor(1));

            auto *condition = inst->getCondition();

            vrblock.back().connect(trueSucc.front(),
                                   new VRAssumeBool(condition, true));
            vrblock.back().connect(falseSucc.front(),
                                   new VRAssumeBool(condition, false));
        }
    }

    void buildSwitch(const llvm::SwitchInst *swtch, VRBBlock vrblock) {
        for (auto &it : swtch->cases()) {
            VRBBlock &succ = codeGraph.getVRBBlock(it.getCaseSuccessor());

            vrblock.back().connect(succ.front(),
                                   new VRAssumeEqual(swtch->getCondition(),
                                                     it.getCaseValue()));
        }

        VRBBlock &succ = codeGraph.getVRBBlock(swtch->getDefaultDest());
        vrblock.back().connect(&succ.front(), new VRNoop());
    }

    void buildReturn(const llvm::ReturnInst *inst, VRBBlock vrblock) {
        vrblock.back().connect(nullptr, new VRInstruction(inst));
    }

    void buildBlock(const llvm::BasicBlock &block) {
        VRBBlockHandle vrblock = codeGraph.newVRBBlock(&block);

        auto it = block.begin();
        const llvm::Instruction *previousInst = &(*it);
        VRLocation &previousLoc =
                codeGraph.newVRLocation(vrblock, previousInst);
        ++it;

        for (; it != block.end(); ++it) {
            const llvm::Instruction &inst = *it;
            VRLocation &newLoc = codeGraph.newVRLocation(vrblock, &inst);

            previousLoc.connect(newLoc, new VRInstruction(previousInst));

            previousInst = &inst;
        }
    }
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_GRAPH_BUILDER_HPP_
