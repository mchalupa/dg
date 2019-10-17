#include <map>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/raw_ostream.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/analysis/PointsTo/PointerGraph.h"
#include "dg/analysis/DFS.h"

#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"
#include "dg/llvm/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "dg/llvm/LLVMNode.h"
#include "dg/llvm/LLVMDependenceGraph.h"

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
        if (auto op = dg->getNode(*I)) {
            // 'node' uses 'op', so we want to add edge 'op'-->'node',
            // that is, 'op' is used in 'node' ('node' is a user of 'op')
            op->addUseDependence(node);
        }
    }
}

static void addReturnEdge(LLVMNode *callNode, LLVMDependenceGraph *subgraph)
{
    // FIXME we may loose some accuracy here and
    // this edges causes that we'll go into subprocedure
    // even with summary edges
    if (!callNode->isVoidTy())
        subgraph->getExit()->addDataDependence(callNode);
}

LLVMDefUseAnalysis::LLVMDefUseAnalysis(LLVMDependenceGraph *dg,
                                       LLVMReachingDefinitions *rd,
                                       LLVMPointerAnalysis *pta)
    : analysis::legacy::DataFlowAnalysis<LLVMNode>(dg->getEntryBB(),
                                                   analysis::legacy::DATAFLOW_INTERPROCEDURAL),
      dg(dg), RD(rd), PTA(pta), DL(new DataLayout(dg->getModule())) {
    assert(PTA && "Need points-to information");
    assert(RD && "Need reaching definitions");
}


void LLVMDefUseAnalysis::handleCallInst(LLVMNode *node)
{
    // add edges from the return nodes of subprocedure
    // to the call (if the call returns something)
    for (LLVMDependenceGraph *subgraph : node->getSubgraphs())
        addReturnEdge(node, subgraph);
}

void LLVMDefUseAnalysis::addDataDependence(LLVMNode *node, llvm::Value *rdval)
{
    LLVMNode *rdnode = dg->getNode(rdval);
    if (!rdnode) {
        // that means that the value is not from this graph.
        // We need to add interprocedural edge
        llvm::Function *F
            = llvm::cast<llvm::Instruction>(rdval)->getParent()->getParent();
        LLVMNode *entryNode = dg->getGlobalNode(F);
        assert(entryNode && "Don't have built function");

        // get the graph where the node lives
        LLVMDependenceGraph *graph = entryNode->getDG();
        assert(graph != dg && "Cannot find a node");
        rdnode = graph->getNode(rdval);
        if (!rdnode) {
            llvmutils::printerr("[DU] error: DG doesn't have val: ", rdval);
            abort();
            return;
        }
    }

    assert(rdnode);
    rdnode->addDataDependence(node);
}

void LLVMDefUseAnalysis::addDataDependence(LLVMNode *node,
                                           const std::vector<llvm::Value *>& defs)
{
    using namespace dg::analysis;
    static std::set<const llvm::Value *> reported_mappings;

    if (defs.empty()) {
        static std::set<const llvm::Value *> reported;
        if (reported.insert(node->getValue()).second) {
            llvm::errs() << "[DU] error: no reaching definition for: "
                         << *node->getValue() << "\n";
        }
        return;
    }

    // add data dependence
    for (auto def : defs) {
        addDataDependence(node, def);
    }
}

bool LLVMDefUseAnalysis::runOnNode(LLVMNode *node, LLVMNode *)
{
    Value *val = node->getKey();

    // just add direct def-use edges to every instruction
    if (auto I = dyn_cast<Instruction>(val))
        handleOperands(I, node);

    if (isa<CallInst>(val)) {
        handleCallInst(node); // return edges and so...
    }

    if (RD->isUse(val)) {
        addDataDependence(node, RD->getLLVMReachingDefinitions(val));
    }

    // we will run only once
    return false;
}

} // namespace dg
