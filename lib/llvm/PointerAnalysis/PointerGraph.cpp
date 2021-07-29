#include <cassert>
#include <set>

#include <llvm/Config/llvm-config.h>

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
#include <llvm/Support/CFG.h>
#else
#include <llvm/IR/CFG.h>
#endif

#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_os_ostream.h>

#include <llvm/IR/Dominators.h>

#include "dg/PointerAnalysis/PointerGraph.h"
#include "dg/PointerAnalysis/PointerGraphOptimizations.h"
#include "dg/llvm/PointerAnalysis/PointerGraph.h"

#include "llvm/PointerAnalysis/PointerGraphValidator.h"
#include "llvm/llvm-utils.h"

#include "dg/util/debug.h"

namespace dg {
namespace pta {

PSNode *LLVMPointerGraphBuilder::getConstant(const llvm::Value *val) {
    using namespace llvm;
    if (isa<ConstantPointerNull>(val) || llvmutils::isConstantZero(val)) {
        return NULLPTR;
    }
    if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(val)) {
        return createConstantExpr(CE).getRepresentant();
    }
    if (isa<Function>(val)) {
        PSNode *ret = PS.create<PSNodeType::FUNCTION>();
        addNode(val, ret);
        return ret;
    }
    if (isa<Constant>(val) || isa<UndefValue>(val)) {
        // it is just some constant that we can not handle
        return UNKNOWN_MEMORY;
    }

    return nullptr;
}

// try get operand, return null if no such value has been constructed
PSNode *LLVMPointerGraphBuilder::tryGetOperand(const llvm::Value *val) {
    auto it = nodes_map.find(val);
    PSNode *op = nullptr;

    if (it != nodes_map.end())
        op = it->second.getRepresentant();

    // if we don't have the operand, then it is a ConstantExpr
    // or some operand of intToPtr instruction (or related to that)
    if (!op) {
        if (llvm::isa<llvm::Constant>(val)) {
            op = getConstant(val);
            if (!op) {
                // unknown constant
                llvm::errs() << "ERR: unhandled constant: " << *val << "\n";
                return nullptr;
            }
        } else
            // unknown operand
            return nullptr;
    }

    // we either found the operand, or we bailed out earlier,
    // so we need to have the operand here
    assert(op && "Did not find an operand");

    // if the operand is a call, use the return node of the call instead
    // - that is the one that contains returned pointers
    if (op->isCall()) {
        op = op->getPairedNode();
    }

    return op;
}

PSNode *LLVMPointerGraphBuilder::getOperand(const llvm::Value *val) {
    PSNode *op = tryGetOperand(val);
    if (!op) {
        if (isInvalid(val, invalidate_nodes))
            return UNKNOWN_MEMORY;

        llvm::errs() << "ERROR: missing value in graph: " << *val << "\n";
        abort();
    } else
        return op;
}

PointerSubgraph &
LLVMPointerGraphBuilder::getAndConnectSubgraph(const llvm::Function *F,
                                               const llvm::CallInst *CInst,
                                               PSNode *callNode) {
    // find or build the subgraph for the function F
    PointerSubgraph &subg = createOrGetSubgraph(F);
    assert(subg.root); // we took the subg by reference, so it should be filled
                       // now

    // setup call edges
    PSNodeCall::cast(callNode)->addCallee(&subg);
    PSNodeEntry *ent = PSNodeEntry::cast(subg.root);
    ent->addCaller(callNode);

    // update callgraph
    const auto *callerF = CInst->getParent()->getParent();
    PS.registerCall(getPointsToNode(callerF), getPointsToNode(F));
    DBG(pta, "CallGraph: " << callerF->getName().str() << " -> "
                           << F->getName().str());

    return subg;
}

LLVMPointerGraphBuilder::PSNodesSeq &
LLVMPointerGraphBuilder::createCallToFunction(const llvm::CallInst *CInst,
                                              const llvm::Function *F) {
    PSNodeCall *callNode = PSNodeCall::get(PS.create<PSNodeType::CALL>());
    auto &subg = getAndConnectSubgraph(F, CInst, callNode);

    // the operands to the return node (which works as a phi node)
    // are going to be added when the subgraph is built
    PSNodeCallRet *returnNode =
            PSNodeCallRet::get(PS.create<PSNodeType::CALL_RETURN>());

    returnNode->setPairedNode(callNode);
    callNode->setPairedNode(returnNode);

    // this must be after we created the CALL_RETURN node
    if (ad_hoc_building) {
        // add operands to arguments and return nodes
        addInterproceduralOperands(F, subg, CInst, callNode);
    }

    PSNodesSeq seq({callNode, returnNode});
    seq.setRepresentant(returnNode);

    return addNode(CInst, seq);
}

bool LLVMPointerGraphBuilder::callIsCompatible(PSNode *call, PSNode *func) {
    const llvm::CallInst *CI = call->getUserData<llvm::CallInst>();
    const llvm::Function *F = func->getUserData<llvm::Function>();
    assert(CI && "No user data in call node");
    assert(F && "No user data in function node");
    // incompatible prototypes, skip it...
    return llvmutils::callIsCompatible(F, CI);
}

void LLVMPointerGraphBuilder::insertFunctionCall(PSNode *callsite,
                                                 PSNode *called) {
    const llvm::CallInst *CI = callsite->getUserData<llvm::CallInst>();
    const llvm::Function *F = called->getUserData<llvm::Function>();

    if (F->isDeclaration()) {
        /// memory allocation (malloc, calloc, etc.)
        auto seq = createUndefFunctionCall(CI, F);
        // we must manually set the data of representant,
        // as we didn't call addNode
        auto *repr = seq.getRepresentant();
        repr->setUserData(const_cast<llvm::CallInst *>(CI));
        // add internal successors
        PSNodesSequenceAddSuccessors(seq);

        callsite->addSuccessor(seq.getFirst());
        auto *retval = callsite->getPairedNode();
        seq.getLast()->addSuccessor(retval);

        retval->addOperand(seq.getRepresentant());
        return;
    }

    PointerSubgraph &subg = getAndConnectSubgraph(F, CI, callsite);

    // remove the CFG edge and keep only the call edge
    if (callsite->successorsNum() == 1 &&
        callsite->getSingleSuccessor() == callsite->getPairedNode()) {
        callsite->removeSingleSuccessor();
    }

    assert(ad_hoc_building && "This should be called with ad_hoc_building");
    // add operands to arguments and return nodes
    addInterproceduralOperands(F, subg, CI, callsite);
}

std::vector<PSNode *>
LLVMPointerGraphBuilder::getPointsToFunctions(const llvm::Value *calledValue) {
    using namespace llvm;
    std::vector<PSNode *> functions;
    if (isa<Function>(calledValue)) {
        PSNode *node;
        auto iterator = nodes_map.find(calledValue);
        if (iterator == nodes_map.end()) {
            node = PS.create<PSNodeType::FUNCTION>();
            addNode(calledValue, node);
            functions.push_back(node);
        } else {
            functions.push_back(iterator->second.getFirst());
        }
        return functions;
    }

    PSNode *operand = getPointsToNode(calledValue);
    if (operand == nullptr) {
        return functions;
    }

    for (const auto &pointer : operand->pointsTo) {
        if (pointer.isValid() && !pointer.isInvalidated() &&
            isa<Function>(pointer.target->getUserData<Value>())) {
            functions.push_back(pointer.target);
        }
    }
    return functions;
}

PointerSubgraph &
LLVMPointerGraphBuilder::createOrGetSubgraph(const llvm::Function *F) {
    auto it = subgraphs_map.find(F);
    if (it == subgraphs_map.end()) {
        // create a new subgraph
        PointerSubgraph &subg = buildFunction(*F);
        assert(subg.root != nullptr);

        if (ad_hoc_building) {
            addProgramStructure(F, subg);
        }

        return subg;
    }

    assert(it->second != nullptr && "Subgraph is nullptr");
    return *it->second;
}

PointerSubgraph *LLVMPointerGraphBuilder::getSubgraph(const llvm::Function *F) {
    auto it = subgraphs_map.find(F);
    if (it == subgraphs_map.end()) {
        return nullptr;
    }

    assert(it->second != nullptr && "Subgraph is nullptr");
    return it->second;
}

void LLVMPointerGraphBuilder::addPHIOperands(PSNode *node,
                                             const llvm::PHINode *PHI) {
    for (unsigned i = 0, e = PHI->getNumIncomingValues(); i < e; ++i) {
        if (PSNode *op = tryGetOperand(PHI->getIncomingValue(i))) {
            // do not add duplicate operands
            if (!node->hasOperand(op))
                node->addOperand(op);
        }
    }
}

template <typename OptsT>
static bool isRelevantCall(const llvm::Instruction *Inst, bool invalidate_nodes,
                           const OptsT &opts) {
    using namespace llvm;

    // we don't care about debugging stuff
    if (isa<DbgValueInst>(Inst))
        return false;

    const CallInst *CInst = cast<CallInst>(Inst);
#if LLVM_VERSION_MAJOR >= 8
    const Value *calledVal = CInst->getCalledOperand()->stripPointerCasts();
#else
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();
#endif
    const Function *func = dyn_cast<Function>(calledVal);

    if (!func)
        // function pointer call - we need that in PointerGraph
        return true;

    if (func->empty()) {
        if (opts.getAllocationFunction(func->getName().str()) !=
            AllocationFunction::NONE)
            // we need memory allocations
            return true;

        if (func->getName().equals("free"))
            // we need calls of free
            return true;

        if (func->getName().equals("pthread_exit"))
            return true;

        if (func->isIntrinsic())
            return isRelevantIntrinsic(func, invalidate_nodes);

        // it returns something? We want that!
        return !func->getReturnType()->isVoidTy();
    } // we want defined function, since those can contain
    // pointer's manipulation and modify CFG
    return true;

    assert(0 && "We should not reach this");
}

LLVMPointerGraphBuilder::PSNodesSeq &
LLVMPointerGraphBuilder::buildInstruction(const llvm::Instruction &Inst) {
    using namespace llvm;

    PSNodesSeq *seq{nullptr};

    switch (Inst.getOpcode()) {
    case Instruction::Alloca:
        seq = &createAlloc(&Inst);
        break;
    case Instruction::Store:
        seq = &createStore(&Inst);
        break;
    case Instruction::Load:
        seq = &createLoad(&Inst);
        break;
    case Instruction::GetElementPtr:
        seq = &createGEP(&Inst);
        break;
    case Instruction::ExtractValue:
        return createExtract(&Inst);
    case Instruction::Select:
        seq = &createSelect(&Inst);
        break;
    case Instruction::PHI:
        seq = &createPHI(&Inst);
        break;
    case Instruction::BitCast:
    case Instruction::SExt:
    case Instruction::ZExt:
        seq = &createCast(&Inst);
        break;
    case Instruction::PtrToInt:
        seq = &createPtrToInt(&Inst);
        break;
    case Instruction::IntToPtr:
        seq = &createIntToPtr(&Inst);
        break;
    case Instruction::Ret:
        seq = &createReturn(&Inst);
        break;
    case Instruction::Call:
        return createCall(&Inst);
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Trunc:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::Xor:
    case Instruction::FSub:
    case Instruction::FAdd:
    case Instruction::FDiv:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
#if LLVM_VERSION_MAJOR >= 8
    case Instruction::FNeg:
#endif
        // these instructions reinterpert the pointer,
        // nothing better we can do here (I think?)
        seq = &createUnknown(&Inst);
        break;
    case Instruction::Add:
        seq = &createAdd(&Inst);
        break;
    case Instruction::Sub:
    case Instruction::Mul:
        seq = &createArithmetic(&Inst);
        break;
    case Instruction::UIToFP:
    case Instruction::SIToFP:
        seq = &createCast(&Inst);
        break;
    case Instruction::FPToUI:
    case Instruction::FPToSI:
        if (llvmutils::typeCanBePointer(&M->getDataLayout(), Inst.getType()))
            seq = &createCast(&Inst);
        else
            seq = &createUnknown(&Inst);
        break;
    case Instruction::InsertElement:
        return createInsertElement(&Inst);
    case Instruction::ExtractElement:
        return createExtractElement(&Inst);
    case Instruction::AtomicRMW:
        return createAtomicRMW(&Inst);
    case Instruction::ShuffleVector:
        llvm::errs() << "ShuffleVector instruction is not supported, loosing "
                        "precision\n";
        seq = &createUnknown(&Inst);
        break;
    default:
        llvm::errs() << Inst << "\n";
        assert(0 && "Unhandled instruction");
        seq = &createUnknown(&Inst);
    }

    assert(seq && "Did not create instruction");
    return *seq;
}

// is the instruction relevant to points-to analysis?
bool LLVMPointerGraphBuilder::isRelevantInstruction(
        const llvm::Instruction &Inst) {
    using namespace llvm;

    switch (Inst.getOpcode()) {
    case Instruction::ICmp:
    case Instruction::FCmp:
    case Instruction::Br:
    case Instruction::Switch:
    case Instruction::Unreachable:
        return false;
    case Instruction::Call:
        return isRelevantCall(&Inst, invalidate_nodes, _options);
    default:
        return true;
    }

    assert(0 && "Not to be reached");
}

// create a formal argument
LLVMPointerGraphBuilder::PSNodesSeq &
LLVMPointerGraphBuilder::createArgument(const llvm::Argument *farg) {
    PSNode *arg = PS.create<PSNodeType::PHI>();
    return addNode(farg, arg);
}

void LLVMPointerGraphBuilder::checkMemSet(const llvm::Instruction *Inst) {
    using namespace llvm;

    if (!llvmutils::memsetIsZeroInitialization(cast<IntrinsicInst>(Inst))) {
        llvm::errs() << "WARNING: Non-0 memset: " << *Inst << "\n";
        return;
    }

    const Value *src = Inst->getOperand(0)->stripInBoundsOffsets();
    PSNode *op = getOperand(src);

    if (const AllocaInst *AI = dyn_cast<AllocaInst>(src)) {
        // if there cannot be stored a pointer, we can bail out here
        // XXX: what if it is alloca of generic mem (e. g. [100 x i8])
        // and we then store there a pointer? Or zero it and load from it?
        // like:
        // char mem[100];
        // void *ptr = (void *) mem;
        // void *p = *ptr;
        if (llvmutils::tyContainsPointer(AI->getAllocatedType()))
            PSNodeAlloc::cast(op)->setZeroInitialized();
    } else {
        // fallback: create a store that represents memset
        // the store will save null to ptr + Offset::UNKNOWN,
        // so we need to do:
        // G = GEP(op, Offset::UNKNOWN)
        // STORE(null, G)
        buildInstruction(*Inst);
    }
}

// Get llvm BasicBlock's in levels of Dominator Tree (BFS order through the
// dominator tree)
std::vector<const llvm::BasicBlock *>
getBasicBlocksInDominatorOrder(llvm::Function &F) {
    std::vector<const llvm::BasicBlock *> blocks;
    blocks.reserve(F.size());

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
    llvm::DominatorTree DTree;
    DTree.recalculate(F);
#else
    llvm::DominatorTreeWrapperPass wrapper;
    wrapper.runOnFunction(F);
    auto &DTree = wrapper.getDomTree();
#ifndef NDEBUG
    wrapper.verifyAnalysis();
#endif
#endif

    auto *root_node = DTree.getRootNode();
    blocks.push_back(root_node->getBlock());

    std::vector<llvm::DomTreeNode *> to_process;
    to_process.reserve(4);
    to_process.push_back(root_node);

    while (!to_process.empty()) {
        std::vector<llvm::DomTreeNode *> new_to_process;
        new_to_process.reserve(to_process.size());

        for (auto *cur_node : to_process) {
            for (auto *child : *cur_node) {
                new_to_process.push_back(child);
                blocks.push_back(child->getBlock());
            }
        }

        to_process.swap(new_to_process);
    }

    return blocks;
}

void LLVMPointerGraphBuilder::buildArguments(const llvm::Function &F,
                                             PointerSubgraph *parent) {
    for (auto A = F.arg_begin(), E = F.arg_end(); A != E; ++A) {
#ifndef NDEBUG
        PSNode *a = tryGetOperand(&*A);
        // we must not have built this argument before
        // (or it is a number or irelevant value)
        assert(a == nullptr || a == UNKNOWN_MEMORY);
#endif
        auto &arg = createArgument(&*A);
        arg.getSingleNode()->setParent(parent);
    }
}

PointerSubgraph &
LLVMPointerGraphBuilder::buildFunction(const llvm::Function &F) {
    DBG_SECTION_BEGIN(pta, "building function '" << F.getName().str() << "'");

    assert(!getSubgraph(&F) && "We already built this function");
    assert(!F.isDeclaration() && "Cannot build an undefined function");

    // create root and later (an unified) return nodes of this subgraph.
    // These are just for our convenience when building the graph,
    // they can be optimized away later since they are noops
    PSNodeEntry *root = PSNodeEntry::get(PS.create<PSNodeType::ENTRY>());
    assert(root);
    root->setFunctionName(F.getName().str());

    // if the function has variable arguments,
    // then create the node for it
    PSNode *vararg = nullptr;
    if (F.isVarArg()) {
        vararg = PS.create<PSNodeType::PHI>();
    }

    // add record to built graphs here, so that subsequent call of this function
    // from buildPointerGraphBlock won't get stuck in infinite recursive call
    // when this function is recursive
    PointerSubgraph *subg = PS.createSubgraph(root, vararg);
    subgraphs_map[&F] = subg;

    assert(subg->root == root && subg->vararg == vararg);

    // create the arguments
    buildArguments(F, subg);

    root->setParent(subg);
    if (vararg)
        vararg->setParent(subg);

    assert(_funcInfo.find(&F) == _funcInfo.end());
    auto &finfo = _funcInfo[&F];
    auto llvmBlocks =
            getBasicBlocksInDominatorOrder(const_cast<llvm::Function &>(F));

    // build the instructions from blocks
    for (const llvm::BasicBlock *block : llvmBlocks) {
        auto blk = buildPointerGraphBlock(*block, subg);

        if (blk.empty()) {
            continue;
        }

        assert(finfo.llvmBlocks.find(block) == finfo.llvmBlocks.end() &&
               "Already have this block");

        // gather all return nodes
        if ((blk.getLastNode()->getType() == PSNodeType::RETURN)) {
            subg->returnNodes.insert(blk.getLastNode());
        }

        finfo.llvmBlocks.emplace(block, std::move(blk));
    }

    // add operands to PHI nodes. It must be done after all blocks are
    // built, since the PHI gathers values from different blocks
    addPHIOperands(F);

    assert(getSubgraph(&F)->root != nullptr);
    DBG_SECTION_END(pta,
                    "building function '" << F.getName().str() << "' done");
    return *subg;
}

void LLVMPointerGraphBuilder::addProgramStructure() {
    // form intraprocedural program structure (CFG edges)
    for (auto &it : subgraphs_map) {
        const llvm::Function *F = it.first;
        PointerSubgraph *subg = it.second;
        assert(subg && "Subgraph was nullptr");

        // add the CFG edges
        addProgramStructure(F, *subg);

        // add the missing operands (to arguments and return nodes)
        addInterproceduralOperands(F, *subg);
    }
}

PointerGraph *LLVMPointerGraphBuilder::buildLLVMPointerGraph() {
    DBG_SECTION_BEGIN(pta, "building pointer graph");

    // get entry function
    llvm::Function *F = M->getFunction(_options.entryFunction);
    if (!F) {
        llvm::errs() << "Did not find " << _options.entryFunction
                     << " function in module\n";
        abort();
    }

    // first we must build globals, because nodes can use them as operands
    buildGlobals();

    // now we can build rest of the graph
    PointerSubgraph &subg = buildFunction(*F);
    PSNode *root = subg.root;
    assert(root != nullptr);
    (void) root; // c++17 TODO: replace with [[maybe_unused]]

    // fill in the CFG edges
    addProgramStructure();

    // FIXME: set entry procedure, not an entry node
    auto *mainsg = getSubgraph(F);
    assert(mainsg);
    PS.setEntry(mainsg);

    // add root to the call graph
    PS.getCallGraph().createNode(getPointsToNode(F));

#ifndef NDEBUG
    for (const auto &subg : PS.getSubgraphs()) {
        assert(subg->root && "No root in a subgraph");
    }

    LLVMPointerGraphValidator validator(&PS);
    if (validator.validate()) {
        llvm::errs() << validator.getWarnings();

        llvm::errs() << "Pointer Subgraph is broken (right after building)!\n";
        assert(!validator.getErrors().empty());
        llvm::errs() << validator.getErrors();
        // return nullptr;
    } else {
        llvm::errs() << validator.getWarnings();
    }
#endif // NDEBUG

    // set this flag to true, so that createCallToFunction
    // (and all recursive calls to this function)
    // will also add the program structure instead of only
    // building the nodes. This is needed as we have the
    // graph already built and we are now only building
    // newly created subgraphs ad hoc.
    ad_hoc_building = true;

    DBG_SECTION_END(pta, "building pointer graph done");

    return &PS;
}

bool LLVMPointerGraphBuilder::validateSubgraph(bool no_connectivity) const {
    LLVMPointerGraphValidator validator(getPS(), no_connectivity);
    if (validator.validate()) {
        assert(!validator.getErrors().empty());
        llvm::errs() << validator.getErrors();
        return false;
    }
    return true;
}

std::vector<PSNode *>
LLVMPointerGraphBuilder::getFunctionNodes(const llvm::Function *F) const {
    auto it = subgraphs_map.find(F);
    if (it == subgraphs_map.end())
        return {};

    auto nodes =
            getReachableNodes(it->second->root, nullptr, false /* interproc */);
    std::vector<PSNode *> ret;
    ret.reserve(nodes.size());
    std::copy(nodes.begin(), nodes.end(), std::back_inserter(ret));

    return ret;
}

} // namespace pta
} // namespace dg
