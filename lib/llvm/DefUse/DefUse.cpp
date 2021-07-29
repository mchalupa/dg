#include <map>

#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

#include "dg/DFS.h"

#include "dg/llvm/DataDependence/DataDependence.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMNode.h"

#include "llvm/llvm-utils.h"

#include "DefUse.h"

using namespace llvm;

/// --------------------------------------------------
//   Add def-use edges
/// --------------------------------------------------
namespace dg {

/// Add def-use edges between instruction and its operands
static void handleOperands(const Instruction *Inst, LLVMNode *node) {
    LLVMDependenceGraph *dg = node->getDG();
    assert(Inst == node->getKey());

    for (auto I = Inst->op_begin(), E = Inst->op_end(); I != E; ++I) {
        auto *op = dg->getNode(*I);
        if (!op)
            continue;
        const auto &subs = op->getSubgraphs();
        if (!subs.empty() && !op->isVoidTy()) {
            for (auto *s : subs) {
                s->getExit()->addDataDependence(node);
            }
        }
        // 'node' uses 'op', so we want to add edge 'op'-->'node',
        // that is, 'op' is used in 'node' ('node' is a user of 'op')
        op->addUseDependence(node);
    }
}

LLVMDefUseAnalysis::LLVMDefUseAnalysis(LLVMDependenceGraph *dg,
                                       LLVMDataDependenceAnalysis *rd,
                                       LLVMPointerAnalysis *pta)
        : legacy::DataFlowAnalysis<LLVMNode>(dg->getEntryBB(),
                                             legacy::DATAFLOW_INTERPROCEDURAL),
          dg(dg), RD(rd), PTA(pta), DL(new DataLayout(dg->getModule())) {
    assert(PTA && "Need points-to information");
    assert(RD && "Need reaching definitions");
}

void LLVMDefUseAnalysis::addDataDependencies(LLVMNode *node) {
    static std::set<const llvm::Value *> reported_mappings;

    auto *val = node->getValue();
    auto defs = RD->getLLVMDefinitions(val);

    // add data dependence
    for (auto *def : defs) {
        LLVMNode *rdnode = dg->getNode(def);
        if (!rdnode) {
            // that means that the value is not from this graph.
            // We need to add interprocedural edge
            llvm::Function *F = llvm::cast<llvm::Instruction>(def)
                                        ->getParent()
                                        ->getParent();
            LLVMNode *entryNode = dg->getGlobalNode(F);
            assert(entryNode && "Don't have built function");

            // get the graph where the node lives
            LLVMDependenceGraph *graph = entryNode->getDG();
            assert(graph != dg && "Cannot find a node");
            rdnode = graph->getNode(def);
            if (!rdnode) {
                llvmutils::printerr("[DU] error: DG doesn't have val: ", def);
                abort();
                return;
            }
        }

        assert(rdnode);
        rdnode->addDataDependence(node);
    }
}

bool LLVMDefUseAnalysis::runOnNode(LLVMNode *node, LLVMNode * /*prev*/) {
    Value *val = node->getKey();

    // just add direct def-use edges to every instruction
    if (auto *I = dyn_cast<Instruction>(val))
        handleOperands(I, node);

    if (RD->isUse(val)) {
        addDataDependencies(node);
    }

    // we will run only once
    return false;
}

} // namespace dg
