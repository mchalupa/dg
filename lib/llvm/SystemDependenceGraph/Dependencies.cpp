#include "dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h"
#include "dg/util/debug.h"

namespace dg {
namespace llvmdg {

///
// Fill in dependence edges into SDG
struct SDGDependenciesBuilder {
    llvmdg::SystemDependenceGraph& _sdg;

    SDGDependenciesBuilder(llvmdg::SystemDependenceGraph& g) : _sdg(g) {}

    void processInstr(llvm::Instruction& I) {
        llvm::errs() << "I: " << I << "\n";
        auto *nd = _sdg.getNode(&I);
        assert(nd && "Do not have node");

        if (llvm::isa<llvm::DbgInfoIntrinsic>(&I)) {
            // FIXME
            llvm::errs() << "sdg: Skipping " << I << "\n";
            return;
        }

        // add 'use' dependencies
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
                /*
                if (!opnode) {
                    if (!opnode) {
                        llvm::errs() << "[SDG error] unhandled operand:\n";
                        llvm::errs() << *opnd << "\n";
                        abort();
                    }
                }
                */
                assert(opnode && "Wrong type of node");
                sdg::DGNode::get(nd)->addUses(*sdg::DGNode::get(opnd));
            }
        }
    }

    void processDG(llvm::Function& F) {
        auto *dg = _sdg.getDG(&F);
        assert(dg && "Do not have dg");

        for (auto& B : F) {
            for (auto& I : B) {
                processInstr(I);
            }
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

    SDGDependenciesBuilder builder(*this);
    builder.processFuns();

    DBG_SECTION_END(sdg, "Adding edges into SDG finished");
}

} // namespace llvmdg
} // namespace dg
