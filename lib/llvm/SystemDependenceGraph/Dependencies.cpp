#include "dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h"
#include "dg/llvm/DataDependence/DataDependence.h"
#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "dg/util/debug.h"

namespace dg {
namespace llvmdg {

///
// Fill in dependence edges into SDG
struct SDGDependenciesBuilder {
    llvmdg::SystemDependenceGraph& _sdg;
    dda::LLVMDataDependenceAnalysis *DDA;
    LLVMControlDependenceAnalysis *CDA;

    SDGDependenciesBuilder(llvmdg::SystemDependenceGraph& g,
                           dda::LLVMDataDependenceAnalysis *dda,
                           LLVMControlDependenceAnalysis *cda)
        : _sdg(g), DDA(dda), CDA(cda) {}

    void addUseDependencies(sdg::DGElement *nd, llvm::Instruction& I) {
        for (auto& op : I.operands()) {
            auto *val = &*op;
            llvm::errs() << I << " -> " << *val << "\n";
            if (llvm::isa<llvm::ConstantExpr>(val)) {
                val = val->stripPointerCasts();
            }
            auto *opnd = _sdg.getNode(val);
            if (!opnd) {
                if (llvm::isa<llvm::ConstantInt>(val) ||
                    llvm::isa<llvm::Function>(val)) {
                    continue;
                }

                llvm::errs() << "[SDG error] Do not have operand node:\n";
                llvm::errs() << *val << "\n";
                abort();
            }

            assert(opnd && "Do not have operand node");
            assert(sdg::DGNode::get(nd) && "Wrong type of node");

            if (auto *arg = sdg::DGArgumentPair::get(opnd)) {
                sdg::DGNode::get(nd)->addUses(arg->getInputArgument());
            } else {
                auto *opnode = sdg::DGNode::get(opnd);
                assert(opnode && "Wrong type of node");
                sdg::DGNode::get(nd)->addUses(*sdg::DGNode::get(opnd));
            }
        }
    }

    void addControlDependencies(sdg::DepDGElement *elem, llvm::Instruction& I) {
        assert(elem);
        for (auto *dep : CDA->getDependencies(&I)) {
            if (auto *depB = llvm::dyn_cast<llvm::BasicBlock>(dep)) {
                auto *depblock = _sdg.getBBlock(depB);
                assert(depblock && "Do not have the block");
                elem->addControlDep(*depblock);
            } else {
                auto *depnd = sdg::DepDGElement::get(_sdg.getNode(dep));
                assert(depnd && "Do not have the node");
                elem->addControlDep(*depnd);
            }
        }
    }

    void addControlDependencies(sdg::DGBBlock *block, llvm::BasicBlock& B) {
        assert(block);
        for (auto *dep : CDA->getDependencies(&B)) {
            if (auto *depB = llvm::dyn_cast<llvm::BasicBlock>(dep)) {
                auto *depblock = _sdg.getBBlock(depB);
                assert(depblock && "Do not have the block");
                block->addControlDep(*depblock);
            } else {
                auto *depnd = sdg::DepDGElement::get(_sdg.getNode(dep));
                assert(depnd && "Do not have the node");
                block->addControlDep(*depnd);
            }
        }
    }

    void addDataDependencies(sdg::DGElement *nd, llvm::Instruction& I) {
        addInterprocDataDependencies(nd, I);
    }

    void addInterprocDataDependencies(sdg::DGElement *nd, llvm::Instruction& I) {
        if (!DDA->isUse(&I))
            return;

        for (auto& op : DDA->getLLVMDefinitions(&I)) {
            auto *val = &*op;
            llvm::errs() << *val << " d-> " << I << "\n";
            auto *opnd = _sdg.getNode(val);
            if (!opnd) {
                llvm::errs() << "[SDG error] Do not have operand node:\n";
                llvm::errs() << *val << "\n";
                abort();
            }

            assert(opnd && "Do not have operand node");
            assert(sdg::DGNode::get(nd) && "Wrong type of node");

            if (auto *arg = sdg::DGArgumentPair::get(opnd)) {
                sdg::DGNode::get(nd)->addMemoryDep(arg->getInputArgument());
            } else {
                auto *opnode = sdg::DGNode::get(opnd);
                assert(opnode && "Wrong type of node");
                sdg::DGNode::get(nd)->addMemoryDep(*sdg::DGNode::get(opnd));
            }
        }
    }

    void processInstr(llvm::Instruction& I) {
        llvm::errs() << "I: " << I << "\n";
        auto *nd = sdg::DepDGElement::get(_sdg.getNode(&I));
        assert(nd && "Do not have node");

        if (llvm::isa<llvm::DbgInfoIntrinsic>(&I)) {
            // FIXME
            llvm::errs() << "sdg: Skipping " << I << "\n";
            return;
        }

        // add dependencies
        addUseDependencies(nd, I);
        addDataDependencies(nd, I);
        addControlDependencies(nd, I);
    }

    void processDG(llvm::Function& F) {
        auto *dg = _sdg.getDG(&F);
        assert(dg && "Do not have dg");

        for (auto& B : F) {
            for (auto& I : B) {
                processInstr(I);
            }

            // block-based control dependencies
            addControlDependencies(_sdg.getBBlock(&B), B);
        }
    }

    void processFuns() {
        for (auto& F : *_sdg.getModule()) {
            if (F.isDeclaration()) {
                continue;
            }

            processDG(F);
        }
    }
};

void SystemDependenceGraph::buildEdges() {
    DBG_SECTION_BEGIN(sdg, "Adding edges into SDG");

    SDGDependenciesBuilder builder(*this, _dda, _cda);
    builder.processFuns();

    DBG_SECTION_END(sdg, "Adding edges into SDG finished");
}

} // namespace llvmdg
} // namespace dg
