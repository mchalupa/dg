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

#include "dg/analysis/PointsTo/PointerSubgraph.h"
#include "dg/analysis/DFS.h"

#include "dg/llvm/analysis/DefUse/LLVMDefUseAnalysisOptions.h"
#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"
#include "dg/llvm/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/llvm/analysis/DefUse/DefUse.h"

#include "dg/llvm/LLVMNode.h"
#include "dg/llvm/LLVMDependenceGraph.h"

#include "llvm/llvm-utils.h"

using dg::analysis::rd::LLVMReachingDefinitions;
using dg::analysis::rd::RDNode;
using dg::analysis::rd::RDNodeType;
using dg::analysis::Offset;

using namespace llvm;

/// --------------------------------------------------
//   Add def-use edges
/// --------------------------------------------------
namespace dg {

/// Add def-use edges between instruction and its operands
static void handleInstruction(const Instruction *Inst, LLVMNode *node)
{
    LLVMDependenceGraph *dg = node->getDG();
    assert(Inst == node->getKey());

    for (auto I = Inst->op_begin(), E = Inst->op_end(); I != E; ++I) {
        if (LLVMNode *op = dg->getNode(*I)) {
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
                                       LLVMPointerAnalysis *pta,
                                       const analysis::LLVMDefUseAnalysisOptions& opts)
    : analysis::legacy::DataFlowAnalysis<LLVMNode>(dg->getEntryBB(),
                                                   analysis::legacy::DATAFLOW_INTERPROCEDURAL),
      dg(dg), RD(rd), PTA(pta), DL(new DataLayout(dg->getModule())), _options(opts)
{
    assert(PTA && "Need points-to information");
    assert(RD && "Need reaching definitions");
}

void LLVMDefUseAnalysis::handleInlineAsm(LLVMNode *callNode)
{
    CallInst *CI = cast<CallInst>(callNode->getValue());
    LLVMDependenceGraph *dg = callNode->getDG();

    // the last operand is the asm itself, so iterate only to e - 1
    for (unsigned i = 0, e = CI->getNumOperands(); i < e - 1; ++i) {
        Value *opVal = CI->getOperand(i);
        if (!opVal->getType()->isPointerTy())
            continue;

        LLVMNode *opNode = dg->getNode(opVal->stripInBoundsOffsets());
        if (!opNode) {
            // FIXME: ConstantExpr
            llvmutils::printerr("WARN: unhandled inline asm operand: ", opVal);
            continue;
        }

        assert(opNode && "Do not have an operand for inline asm");

        // if nothing else, this call at least uses the operands
        opNode->addDataDependence(callNode);
    }
}

void LLVMDefUseAnalysis::handleIntrinsicCall(LLVMNode *callNode,
                                             CallInst *CI)
{
    static std::set<Instruction *> warnings;
    IntrinsicInst *I = cast<IntrinsicInst>(CI);
    Value *dest, *src = nullptr;

    switch (I->getIntrinsicID())
    {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
            src = I->getOperand(1);
            // fall-through
        case Intrinsic::memset:
        case Intrinsic::vastart:
            dest = I->getOperand(0);
            break;
        case Intrinsic::vaend:
        case Intrinsic::lifetime_start:
        case Intrinsic::lifetime_end:
        case Intrinsic::trap:
            // nothing to be done here
            return;
        case Intrinsic::bswap:
        case Intrinsic::prefetch:
        case Intrinsic::objectsize:
        case Intrinsic::sadd_with_overflow:
        case Intrinsic::uadd_with_overflow:
        case Intrinsic::ssub_with_overflow:
        case Intrinsic::usub_with_overflow:
        case Intrinsic::smul_with_overflow:
        case Intrinsic::umul_with_overflow:
            // nothing to be done, direct def-use edges
            // will be added later
            assert(I->getCalledFunction()->doesNotAccessMemory());
            return;
        case Intrinsic::stacksave:
        case Intrinsic::stackrestore:
            if (warnings.insert(CI).second)
                llvmutils::printerr("WARN: stack save/restore not implemented", CI);
            return;
        default:
            llvmutils::printerr("WARNING: unhandled intrinsic call", I);
            // if it does not access memory, we can just add
            // direct def-use edges
            if (I->getCalledFunction()->doesNotAccessMemory())
                return;

            assert (0 && "Unhandled intrinsic that accesses memory");
            // for release builds, do the best we can here
            handleUndefinedCall(callNode, CI);
            return;
    }

    // we must have dest set
    assert(dest);

    // these functions touch the memory of the pointers
    addDataDependence(callNode, CI, dest, Offset::UNKNOWN /* FIXME */);

    if (src)
        addDataDependence(callNode, CI, src, Offset::UNKNOWN /* FIXME */);
}

void LLVMDefUseAnalysis::handleUndefinedCall(LLVMNode *callNode, CallInst *CI)
{
    if (_options.undefinedArePure)
        return;

    // the function is undefined - add the top-level dependencies and
    // also assume that this function use all the memory that is passed
    // via the pointers
    for (int e = CI->getNumArgOperands(), i = 0; i < e; ++i) {
        if (auto pts = PTA->getPointsTo(CI->getArgOperand(i))) {
            // the passed memory may be used in the undefined
            // function on the unknown offset
            addDataDependence(callNode, CI, pts, Offset::UNKNOWN);
        }
    }
}

void LLVMDefUseAnalysis::handleCallInst(LLVMNode *node)
{
    CallInst *CI = cast<CallInst>(node->getKey());

    if (CI->isInlineAsm()) {
        handleInlineAsm(node);
        return;
    }

    Function *func
        = dyn_cast<Function>(CI->getCalledValue()->stripPointerCasts());
    if (func) {
        if (func->isIntrinsic() && !isa<DbgInfoIntrinsic>(CI)) {
            handleIntrinsicCall(node, CI);
            return;
        }

        // for realloc, we need to make it data dependent on the
        // memory it reallocates, since that is the memory it copies
        if (func->size() == 0) {
            using analysis::AllocationFunction;
            auto type = _options.getAllocationFunction(func->getName());

            if (type == AllocationFunction::REALLOC) {
                addDataDependence(node, CI, CI->getOperand(0), Offset::UNKNOWN /* FIXME */);
            } else if (type == AllocationFunction::NONE) {
                handleUndefinedCall(node, CI);
            }// else {
             // we do not want to do anything for the memory
             // allocation functions
             // }

            // the function is undefined, so do not even try to
            // add the edges from return statements
            return;
        }
    }

    // add edges from the return nodes of subprocedure
    // to the call (if the call returns something)
    for (LLVMDependenceGraph *subgraph : node->getSubgraphs())
        addReturnEdge(node, subgraph);
}

// Add data dependence edges from all memory location that may write
// to memory pointed by 'pts' to 'node'
void LLVMDefUseAnalysis::addUnknownDataDependence(LLVMNode *node, PSNode *pts)
{
    // iterate over all nodes from ReachingDefinitions Subgraph. It is faster than
    // going over all llvm nodes and querying the pointer to analysis
    for (auto& it : RD->getNodesMap()) {
        RDNode *rdnode = it.second;

        // only STORE may be a definition site
        if (rdnode->getType() != analysis::rd::RDNodeType::STORE)
            continue;

        llvm::Value *rdVal = rdnode->getUserData<llvm::Value>();
        // artificial node?
        if (!rdVal)
            continue;

        // does this store define some value that is in pts?
        for (const analysis::rd::DefSite& ds : rdnode->getDefines()) {
            llvm::Value *llvmVal = ds.target->getUserData<llvm::Value>();
            // is this an artificial node?
            if (!llvmVal)
                continue;

            // if these two sets have an over-lap, we must add the data dependence
            for (const auto& ptr : pts->pointsTo)
                if (ptr.target->getUserData<llvm::Value>() == llvmVal) {
                    addDataDependence(node, rdVal);
            }
        }
    }
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


void LLVMDefUseAnalysis::addDataDependence(LLVMNode *node, RDNode *rd)
{
    llvm::Value *rdval = rd->getUserData<llvm::Value>();
    assert(rdval && "RDNode has not set the coresponding value");
    addDataDependence(node, rdval);
}

// \param mem   current reaching definitions point
void LLVMDefUseAnalysis::addDataDependence(LLVMNode *node, PSNode *pts,
                                           RDNode *mem, uint64_t size)
{
    using namespace dg::analysis;
    static std::set<const llvm::Value *> reported_mappings;

    for (const pta::Pointer& ptr : pts->pointsTo) {
        if (!ptr.isValid())
            continue;

        if (ptr.isInvalidated())
            continue;

        llvm::Value *llvmVal = ptr.target->getUserData<llvm::Value>();
        assert(llvmVal && "Don't have Value in PSNode");

        RDNode *val = RD->getNode(llvmVal);
        if(!val) {
            if (reported_mappings.insert(llvmVal).second)
                llvmutils::printerr("DEF-USE: no information for: ", llvmVal);

            // XXX: shouldn't we set val to unknown location now?
            continue;
        }

        std::set<RDNode *> defs;
        // Get even reaching definitions for UNKNOWN_MEMORY.
        // Since those can be ours definitions, we must add them always
        mem->getReachingDefinitions(rd::UNKNOWN_MEMORY, Offset::UNKNOWN,
                                    Offset::UNKNOWN, defs);
        if (!defs.empty()) {
            for (RDNode *rd : defs) {
                assert(!rd->isUnknown() && "Unknown memory defined at unknown location?");
                if (rd->getType() != RDNodeType::PHI)
                    addDataDependence(node, rd);
            }

            defs.clear();
        }

        mem->getReachingDefinitions(val, ptr.offset, size, defs);
        if (defs.empty()) {
            llvm::GlobalVariable *GV
                = llvm::dyn_cast<llvm::GlobalVariable>(llvmVal);
            if (!GV || !GV->hasInitializer()) {
                static std::set<const llvm::Value *> reported;
                if (reported.insert(llvmVal).second) {
                    llvm::errs() << "No reaching definition for: " << *llvmVal;
                    const llvm::Value *val = mem->getUserData<llvm::Value>();
                    if (val)
                        llvm::errs() << " in: " << *val;
                    llvm::errs() << " off: " << *ptr.offset << "\n";
                }
            } else {
                // this is global variable and the last definition
                // is the initialization

                LLVMDependenceGraph *dg = node->getDG();
                assert(dg);

                LLVMNode *global_param_node = dg->getNode(GV);
                assert(global_param_node);

                global_param_node->addDataDependence(node);
            }

            continue;
        }

        // add data dependence
        for (RDNode *rd : defs) {
            if (rd->isUnknown()) {
                // we don't know what definitions reach this node,
                // se we must add data dependence to all possible
                // write to this memory
                addUnknownDataDependence(node, pts);

                // we can bail out, since we have added all
                break;
            }

            if (rd->getType() != RDNodeType::PHI)
                addDataDependence(node, rd);
        }
    }
}

void LLVMDefUseAnalysis::addDataDependence(LLVMNode *node,
                                           const llvm::Value *where, /* in CFG */
                                           const llvm::Value *ptrOp,
                                           uint64_t size)
{
    // get points-to information for the operand
    PSNode *pts = PTA->getPointsTo(ptrOp);
    if (!pts) {
        llvmutils::printerr("[DU] error: no points-to: ", ptrOp);
        return;
    }

    addDataDependence(node, where, pts, size);
}

void LLVMDefUseAnalysis::addDataDependence(LLVMNode *node,
                                           const llvm::Value *where, /* in CFG */
                                           PSNode *pts, /* what memory */
                                           uint64_t size)
{
    using namespace dg::analysis;

    // get the node from reaching definition where we have
    // all the reaching definitions
    RDNode *mem = RD->getMapping(where);
    if(!mem) {
        llvmutils::printerr("[DU] error: don't have mapping: ", where);
        return;
    }

    // take every memory the load inst can use and get the
    // reaching definition
    addDataDependence(node, pts, mem, size);
}

static uint64_t getAllocatedSize(llvm::Type *Ty, const llvm::DataLayout *DL)
{
    // Type can be i8 *null or similar
    if (!Ty->isSized())
            return Offset::UNKNOWN;

    return DL->getTypeAllocSize(Ty);
}

void LLVMDefUseAnalysis::handleLoadInst(llvm::LoadInst *Inst, LLVMNode *node)
{
    using namespace dg::analysis;

    uint64_t size = getAllocatedSize(Inst->getType(), DL);
    addDataDependence(node, Inst, Inst->getPointerOperand(), size);
}

bool LLVMDefUseAnalysis::runOnNode(LLVMNode *node, LLVMNode *)
{
    Value *val = node->getKey();

    if (LoadInst *Inst = dyn_cast<LoadInst>(val)) {
        handleLoadInst(Inst, node);
    } else if (isa<CallInst>(val)) {
        handleCallInst(node);
    }

    /* just add direct def-use edges to every instruction */
    if (Instruction *Inst = dyn_cast<Instruction>(val))
        handleInstruction(Inst, node);

    // we will run only once
    return false;
}

} // namespace dg
