#ifndef DG_LLVM_VALUE_RELATIONS_GRAPH_BUILDER_HPP_
#define DG_LLVM_VALUE_RELATIONS_GRAPH_BUILDER_HPP_

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include "llvm/IR/Constants.h"
#include <llvm/IR/CFG.h>

#include "GraphElements.h"

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace dg {
namespace vr {

struct GraphBuilder {
    const llvm::Module& module;
    unsigned last_node_id = 0;

    // VRLocation corresponding to the state of the program BEFORE executing the instruction
    std::map<const llvm::Instruction *, VRLocation *>& locationMapping;
    std::map<const llvm::BasicBlock *, std::unique_ptr<VRBBlock>>& blockMapping;

    GraphBuilder(const llvm::Module& m,
                 std::map<const llvm::Instruction *, VRLocation *>& locs,
                 std::map<const llvm::BasicBlock *, std::unique_ptr<VRBBlock>>& blcs)
                 : module(m), locationMapping(locs), blockMapping(blcs) {}
                 
    void build() {
        for (const llvm::Function& f : module) {
            build(f);
        }
    }

    void build(const llvm::Function& function) {
        for (const llvm::BasicBlock& block : function) {
            assert(block.size() != 0);
            build(block);
        }

        for (const llvm::BasicBlock& block : function) {
            VRBBlock* vrblock = getVRBBlock(&block);
            assert(vrblock);

            const llvm::Instruction* terminator = block.getTerminator();
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

    void buildSwitch(const llvm::SwitchInst* swtch, VRBBlock* vrblock) {
        for (auto& it : swtch->cases()) {
            VRBBlock* succ = getVRBBlock(it.getCaseSuccessor());
            assert(succ);

            auto op = std::unique_ptr<VROp>(new VRAssumeEqual(swtch->getCondition(), it.getCaseValue()));
            VREdge* edge = new VREdge(vrblock->last(), succ->first(), std::move(op));
            vrblock->last()->connect(std::unique_ptr<VREdge>(edge));
        }

        VRBBlock* succ = getVRBBlock(swtch->getDefaultDest());
        assert(succ);
        auto op = std::unique_ptr<VROp>(new VRNoop());
        VREdge* edge = new VREdge(vrblock->last(), succ->first(), std::move(op));
        vrblock->last()->connect(std::unique_ptr<VREdge>(edge));

    }

    void buildBranch(const llvm::BranchInst* inst, VRBBlock* vrblock) {
        if (inst->isUnconditional()) {
            VRBBlock* succ = getVRBBlock(inst->getSuccessor(0));
            assert(succ);

            auto op = std::unique_ptr<VROp>(new VRNoop());
            VREdge* edge = new VREdge(vrblock->last(), succ->first(), std::move(op));

            vrblock->last()->connect(std::unique_ptr<VREdge>(edge));
        } else {
            VRBBlock* trueSucc = getVRBBlock(inst->getSuccessor(0));
            VRBBlock* falseSucc = getVRBBlock(inst->getSuccessor(1));

            auto trueOp = std::unique_ptr<VROp>(new VRAssumeBool(inst->getCondition(), true));
            auto falseOp = std::unique_ptr<VROp>(new VRAssumeBool(inst->getCondition(), false));

            VREdge* trueEdge = new VREdge(vrblock->last(), trueSucc->first(), std::move(trueOp));
            VREdge* falseEdge = new VREdge(vrblock->last(), falseSucc->first(), std::move(falseOp));

            vrblock->last()->connect(std::unique_ptr<VREdge>(trueEdge));
            vrblock->last()->connect(std::unique_ptr<VREdge>(falseEdge));
        }
    }

    void buildReturn(const llvm::ReturnInst* inst, VRBBlock* vrblock) {
        auto op = std::unique_ptr<VROp>(new VRInstruction(inst));
        VREdge* edge = new VREdge(vrblock->last(), nullptr, std::move(op));

        vrblock->last()->connect(std::unique_ptr<VREdge>(edge));
    }

    void build(const llvm::BasicBlock& block) {
        VRBBlock* vrblock = newBBlock(&block);

        auto it = block.begin();
        const llvm::Instruction* previous = &(*it);
        vrblock->append(newLocation(previous));
        ++it;

        for (; it != block.end(); ++it) {
            const llvm::Instruction& inst = *it;
            VRLocation* newLoc = newLocation(&inst);

            VREdge* edge = new VREdge(vrblock->last(), newLoc,
                                   std::unique_ptr<VROp>(new VRInstruction(previous)));
            vrblock->last()->connect(std::unique_ptr<VREdge>(edge));

            vrblock->append(newLoc);
            previous = &inst;
        }
    }

    VRLocation *newLocation(const llvm::Instruction* inst) {
        assert(inst);
        assert(locationMapping.find(inst) == locationMapping.end());

        auto location = new VRLocation(++last_node_id);
        assert(location);

        locationMapping.emplace(inst, location);
        return location;
    }

    VRBBlock *newBBlock(const llvm::BasicBlock* B) {
        assert(B);
        assert(blockMapping.find(B) == blockMapping.end());

        auto block = new VRBBlock();
        assert(block);

        blockMapping.emplace(B, std::unique_ptr<VRBBlock>(block));
        return block;
    }

    VRBBlock *getVRBBlock(const llvm::BasicBlock* B) {
        const GraphBuilder* constThis = this;
        return const_cast<VRBBlock*>(constThis->getVRBBlock(B));
    }

    const VRBBlock *getVRBBlock(const llvm::BasicBlock* B) const {
        auto it = blockMapping.find(B);
        return it == blockMapping.end() ? nullptr : it->second.get();
    }

    VRLocation *getVRLocation(const llvm::Instruction* inst) {
        const GraphBuilder* constThis = this;
        return const_cast<VRLocation*>(constThis->getVRLocation(inst));
    }

    const VRLocation *getVRLocation(const llvm::Instruction* inst) const {
        auto it = locationMapping.find(inst);
        return it == locationMapping.end() ? nullptr : it->second;
    }

};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_GRAPH_BUILDER_HPP_
