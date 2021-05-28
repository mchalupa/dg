#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "dg/llvm/DataDependence/DataDependence.h"
#include "dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h"
#include "dg/util/debug.h"

namespace dg {
namespace llvmdg {

///
// Fill in dependence edges into SDG
struct SDGDependenciesBuilder {
    llvmdg::SystemDependenceGraph &_sdg;
    dda::LLVMDataDependenceAnalysis *DDA;
    LLVMControlDependenceAnalysis *CDA;

    SDGDependenciesBuilder(llvmdg::SystemDependenceGraph &g,
                           dda::LLVMDataDependenceAnalysis *dda,
                           LLVMControlDependenceAnalysis *cda)
            : _sdg(g), DDA(dda), CDA(cda) {}

    void addUseDependencies(sdg::DGElement *nd, llvm::Instruction &I) {
        for (auto &op : I.operands()) {
            auto *val = &*op;
            if (llvm::isa<llvm::ConstantExpr>(val)) {
                val = val->stripPointerCasts();
            } else if (llvm::isa<llvm::BasicBlock>(val) ||
                       llvm::isa<llvm::ConstantInt>(val)) {
                // we do not add use edges to basic blocks
                // FIXME: but maybe we could? The implementation could be then
                // clearer...
                continue;
            }

            auto *opnd = _sdg.getNode(val);
            if (!opnd) {
                if (auto *fun = llvm::dyn_cast<llvm::Function>(val)) {
                    llvm::errs() << "[SDG error] Do not have fun as operand: "
                                 << fun->getName() << "\n";
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
                sdg::DGNode::get(nd)->addUses(*opnode);
            }
        }
    }

    // elem is CD on 'on'
    void addControlDep(sdg::DepDGElement *elem, const llvm::Value *on) {
        if (const auto *depB = llvm::dyn_cast<llvm::BasicBlock>(on)) {
            auto *depblock = _sdg.getBBlock(depB);
            assert(depblock && "Do not have the block");
            elem->addControlDep(*depblock);
        } else {
            auto *depnd = sdg::DepDGElement::get(_sdg.getNode(on));
            assert(depnd && "Do not have the node");

            if (auto *C = sdg::DGNodeCall::get(depnd)) {
                // this is 'noret' dependence (we have no other control deps for
                // calls)
                auto *noret = C->getParameters().getNoReturn();
                if (!noret)
                    noret = &C->getParameters().createNoReturn();
                elem->addControlDep(*noret);

                // add CD to all formal norets
                for (auto *calledF : C->getCallees()) {
                    auto *fnoret = calledF->getParameters().getNoReturn();
                    if (!fnoret)
                        fnoret = &calledF->getParameters().createNoReturn();
                    noret->addControlDep(*fnoret);
                }
            } else {
                elem->addControlDep(*depnd);
            }
        }
    }

    void addControlDependencies(sdg::DepDGElement *elem, llvm::Instruction &I) {
        assert(elem);
        for (auto *dep : CDA->getDependencies(&I)) {
            addControlDep(elem, dep);
        }
    }

    void addControlDependencies(sdg::DGBBlock *block, llvm::BasicBlock &B) {
        assert(block);
        for (auto *dep : CDA->getDependencies(&B)) {
            addControlDep(block, dep);
        }
    }

    void addDataDependencies(sdg::DGElement *nd, llvm::Instruction &I) {
        addInterprocDataDependencies(nd, I);
    }

    void addInterprocDataDependencies(sdg::DGElement *nd,
                                      llvm::Instruction &I) {
        if (!DDA->isUse(&I))
            return;

        for (auto &op : DDA->getLLVMDefinitions(&I)) {
            auto *val = &*op;
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
                sdg::DGNode::get(nd)->addMemoryDep(*opnode);
            }
        }
    }

    void processInstr(llvm::Instruction &I) {
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

    void processDG(llvm::Function &F) {
        auto *dg = _sdg.getDG(&F);
        assert(dg && "Do not have dg");

        for (auto &B : F) {
            for (auto &I : B) {
                processInstr(I);
            }

            // block-based control dependencies
            addControlDependencies(_sdg.getBBlock(&B), B);
        }

        // add noreturn dependencies

        DBG(sdg, "Adding noreturn dependencies to " << F.getName().str());
        auto *noret = dg->getParameters().getNoReturn();
        if (!noret)
            noret = &dg->getParameters().createNoReturn();
        for (auto *dep : CDA->getNoReturns(&F)) {
            llvm::errs() << "NORET: " << *dep << "\n";
            auto *nd = sdg::DepDGElement::get(_sdg.getNode(dep));
            assert(nd && "Do not have the node");
            if (auto *C = sdg::DGNodeCall::get(nd)) {
                // if this is call, add it again to noret node
                auto *cnoret = C->getParameters().getNoReturn();
                assert(cnoret && "Did not create a noret for a call");
                noret->addControlDep(*cnoret);
            } else {
                noret->addControlDep(*nd);
            }
        }
    }

    void processFuns() {
        for (auto &F : *_sdg.getModule()) {
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
