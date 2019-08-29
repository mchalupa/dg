#include <cassert>
#include <set>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Config/llvm-config.h>

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
 #include <llvm/Support/CFG.h>
#else
 #include <llvm/IR/CFG.h>
#endif

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Constant.h>
#include <llvm/Support/raw_os_ostream.h>

#include <llvm/IR/Dominators.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/analysis/PointsTo/PointerGraph.h"
#include "dg/analysis/PointsTo/PointerGraphOptimizations.h"
#include "dg/llvm/analysis/PointsTo/PointerGraph.h"

#include "llvm/analysis/PointsTo/PointerGraphValidator.h"
#include "llvm/llvm-utils.h"

#include "dg/util/debug.h"

namespace dg {
namespace analysis {
namespace pta {

PSNode *LLVMPointerGraphBuilder::getConstant(const llvm::Value *val)
{
    if (llvm::isa<llvm::ConstantPointerNull>(val)
        || isConstantZero(val)) {
        return NULLPTR;
    } else if (llvm::isa<llvm::UndefValue>(val)) {
        return UNKNOWN_MEMORY;
    } else if (const llvm::ConstantExpr *CE
                    = llvm::dyn_cast<llvm::ConstantExpr>(val)) {
        return createConstantExpr(CE);
    } else if (llvm::isa<llvm::Function>(val)) {
        PSNode *ret = PS.create(PSNodeType::FUNCTION);
        addNode(val, ret);
        return ret;
    } else if (llvm::isa<llvm::Constant>(val)) {
        // it is just some constant that we can not handle
        return UNKNOWN_MEMORY;
    } else
        return nullptr;
}

// try get operand, return null if no such value has been constructed
PSNode *LLVMPointerGraphBuilder::tryGetOperand(const llvm::Value *val)
{
    auto it = nodes_map.find(val);
    PSNode *op = nullptr;

    if (it != nodes_map.end())
        op = it->second.second;

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

PSNode *LLVMPointerGraphBuilder::getOperand(const llvm::Value *val)
{
    PSNode *op = tryGetOperand(val);
    if (!op) {
        if (isInvalid(val, invalidate_nodes))
            return UNKNOWN_MEMORY;

        llvm::errs() << "ERROR: missing value in graph: " << *val << "\n";
        abort();
    } else
        return op;
}

PSNodesSeq
LLVMPointerGraphBuilder::createCallToFunction(const llvm::CallInst *CInst,
                                              const llvm::Function *F)
{
    PSNodeCall *callNode = PSNodeCall::get(PS.create(PSNodeType::CALL));

    // find or build the subgraph for the function F
    PointerSubgraph& subg = createOrGetSubgraph(F);
    assert(subg.root); // we took the subg by reference, so it should be filled now

    // setup call edges
    callNode->addCallee(&subg);
    if (PSNodeEntry *ent = PSNodeEntry::get(subg.root)) {
        ent->addCaller(callNode);
    } else {
        assert(false && "Root is not an entry node");
    }

    // update callgraph
    auto cinstg = getSubgraph(CInst->getParent()->getParent());
    assert(cinstg);
    auto parentEntry = cinstg->root;
    assert(parentEntry);
    PS.registerCall(parentEntry, subg.root);

    // the operands to the return node (which works as a phi node)
    // are going to be added when the subgraph is built
    PSNodeCallRet *returnNode = PSNodeCallRet::get(PS.create(PSNodeType::CALL_RETURN, nullptr));
    assert(returnNode);

    // we will remove this edge later if the procedure does not return
    // (now keep it for simplicity)
    callNode->addSuccessor(returnNode);

    returnNode->setPairedNode(callNode);
    callNode->setPairedNode(returnNode);

    // this must be after we created the CALL_RETURN node
    if (ad_hoc_building) {
        // add operands to arguments and return nodes
        addInterproceduralOperands(F, subg, CInst, callNode);
    }

    return std::make_pair(callNode, returnNode);
}

PSNodesSeq
LLVMPointerGraphBuilder::createFuncptrCall(const llvm::CallInst *CInst,
                                              const llvm::Function *F)
{
    // set this flag to true, so that createCallToFunction
    // (and all recursive calls to this function)
    // will also add the program structure instead of only
    // building the nodes. This is needed as we have the
    // graph already built and we are now only building
    // newly created subgraphs ad hoc.
    ad_hoc_building = true;

    auto ret = createCallToFunction(CInst, F);
#ifndef NDEBUG
    PointerSubgraph *subg = getSubgraph(F);
    assert(subg != nullptr);
    assert(subg->root != nullptr);
#endif

    ad_hoc_building = false;

    return ret;
}

bool
LLVMPointerGraphBuilder::callIsCompatible(PSNode *call,
                                             PSNode *func)
{
    const llvm::CallInst *CI = call->getUserData<llvm::CallInst>();
    const llvm::Function *F = func->getUserData<llvm::Function>();
    // incompatible prototypes, skip it...
    return llvmutils::callIsCompatible(F, CI);
} 

void
LLVMPointerGraphBuilder::insertFunctionCall(PSNode *callsite, PSNode *called)
{ 
    const llvm::CallInst *CI = callsite->getUserData<llvm::CallInst>();
    const llvm::Function *F = called->getUserData<llvm::Function>();

    // create new instructions
    auto cf = createFuncptrCall(CI, F);
    assert(cf.first && "Failed building the subgraph");
    
    // we got the return site for the call stored as the paired node
    PSNode *ret = callsite->getPairedNode();
    if (cf.second) {
        // If we have some returns from this function,
        // pass the returned values to the return site.
        ret->addOperand(cf.second);
        cf.second->addSuccessor(ret);
    }
    
    // Connect the graph to the original graph --
    // replace the edge call->ret that we have added
    // due to the connectivity of the graph.
    // Now we know what is to be called, so we can remove it.
    // We can also replace the edge only when we know
    // that the function will return.
    // If the function does not return, we cannot trim the graph
    // here as this called function may be due to an approximation
    // and the real called function can be established in
    // the following code (if this call is on a cycle).
    if (callsite->successorsNum() == 1 &&
        callsite->getSingleSuccessor() == ret) {
        callsite->replaceSingleSuccessor(cf.first);
    } else {
        // we already have some subgraph connected,
        // so just add a new one
        callsite->addSuccessor(cf.first);
    }
}

void LLVMPointerGraphBuilder::insertPthreadCreateByPtrCall(PSNode *callsite)
{
    ad_hoc_building = true;
    auto seq = createFork(callsite->getUserData<llvm::CallInst>());
    seq.second->addSuccessor(callsite->getSingleSuccessor());
    callsite->replaceSingleSuccessor(seq.first);
    PSNodeFork::cast(seq.second)->setCallInst(callsite);
    ad_hoc_building = false;
}

void LLVMPointerGraphBuilder::insertPthreadJoinByPtrCall(PSNode *callsite)
{
    ad_hoc_building = true;
    auto seq = createJoin(callsite->getUserData<llvm::CallInst>());
    seq.second->addSuccessor(callsite->getSingleSuccessor());
    callsite->replaceSingleSuccessor(seq.first);
    PSNodeJoin::cast(seq.second)->setCallInst(callsite);
    ad_hoc_building = false;
}

std::vector<PSNode *>
LLVMPointerGraphBuilder::getPointsToFunctions(const llvm::Value *calledValue)
{
    using namespace llvm;
    std::vector<PSNode *> functions;
    if (isa<Function>(calledValue)) {
        PSNode *node;
        auto iterator = nodes_map.find(calledValue);
        if (iterator == nodes_map.end()) {
            node = PS.create(PSNodeType::FUNCTION);
            addNode(calledValue, node);
            functions.push_back(node);
        } else {
            functions.push_back(iterator->second.first);
        }
        return functions;
    }

    PSNode *operand = getPointsTo(calledValue);
    if (operand == nullptr) {
        return functions;
    }

    for (const analysis::pta::Pointer pointer : operand->pointsTo) {
        if (pointer.isValid()
                && !pointer.isInvalidated()
                && isa<Function>(pointer.target->getUserData<Value>())) {
            functions.push_back(pointer.target);
        }
    }
    return functions;
}

std::map<const llvm::CallInst *, PSNodeJoin *>
LLVMPointerGraphBuilder::getJoins() const
{
    return threadJoinCalls;
}

std::map<const llvm::CallInst *, PSNodeFork *> LLVMPointerGraphBuilder::getForks() const
{
    return threadCreateCalls;
}

PSNodeJoin *LLVMPointerGraphBuilder::findJoin(const llvm::CallInst *callInst) const
{
    auto iterator = threadJoinCalls.find(callInst);
    if (iterator != threadJoinCalls.end()) {
        return iterator->second;
    }
    return nullptr;
}

PointerSubgraph&
LLVMPointerGraphBuilder::createOrGetSubgraph(const llvm::Function *F)
{
    auto it = subgraphs_map.find(F);
    if (it == subgraphs_map.end()) {
        // create a new subgraph
        PointerSubgraph& subg = buildFunction(*F);
        assert(subg.root != nullptr);

        if (ad_hoc_building) {
            addProgramStructure(F, subg);
        }

        return subg;
    }

    assert(it->second != nullptr && "Subgraph is nullptr");
    return *it->second;
}

PointerSubgraph*
LLVMPointerGraphBuilder::getSubgraph(const llvm::Function *F)
{
    auto it = subgraphs_map.find(F);
    if (it == subgraphs_map.end()) {
        return nullptr;
    }

    assert(it->second != nullptr && "Subgraph is nullptr");
    return it->second;
}


void LLVMPointerGraphBuilder::addPHIOperands(PSNode *node, const llvm::PHINode *PHI)
{
    for (int i = 0, e = PHI->getNumIncomingValues(); i < e; ++i) {
        if (PSNode *op = tryGetOperand(PHI->getIncomingValue(i))) {
            // do not add duplicate operands
            if (!node->hasOperand(op))
                node->addOperand(op);
        }
    }
}

template <typename OptsT>
static bool isRelevantCall(const llvm::Instruction *Inst, bool invalidate_nodes,
                           const OptsT& opts)
{
    using namespace llvm;

    // we don't care about debugging stuff
    if (isa<DbgValueInst>(Inst))
        return false;

    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();
    const Function *func = dyn_cast<Function>(calledVal);

    if (!func)
        // function pointer call - we need that in PointerGraph
        return true;

    if (func->size() == 0) {
        if (opts.getAllocationFunction(func->getName())
            != AllocationFunction::NONE)
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
    } else
        // we want defined function, since those can contain
        // pointer's manipulation and modify CFG
        return true;

    assert(0 && "We should not reach this");
}

PSNodesSeq
LLVMPointerGraphBuilder::buildInstruction(const llvm::Instruction& Inst)
{
    using namespace llvm;
    PSNode *node;

    switch(Inst.getOpcode()) {
        case Instruction::Alloca:
            node = createAlloc(&Inst);
            break;
        case Instruction::Store:
            node = createStore(&Inst);
            break;
        case Instruction::Load:
            node = createLoad(&Inst);
            break;
        case Instruction::GetElementPtr:
            node = createGEP(&Inst);
            break;
        case Instruction::ExtractValue:
            return createExtract(&Inst);
        case Instruction::Select:
            node = createSelect(&Inst);
            break;
        case Instruction::PHI:
            node = createPHI(&Inst);
            break;
        case Instruction::BitCast:
        case Instruction::SExt:
        case Instruction::ZExt:
            node = createCast(&Inst);
            break;
        case Instruction::PtrToInt:
            node = createPtrToInt(&Inst);
            break;
        case Instruction::IntToPtr:
            node = createIntToPtr(&Inst);
            break;
        case Instruction::Ret:
            node = createReturn(&Inst);
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
            // these instructions reinterpert the pointer,
            // nothing better we can do here (I think?)
            node = createUnknown(&Inst);
            break;
        case Instruction::Add:
            node = createAdd(&Inst);
            break;
        case Instruction::Sub:
        case Instruction::Mul:
            node = createArithmetic(&Inst);
            break;
        case Instruction::UIToFP:
        case Instruction::SIToFP:
            node = createCast(&Inst);
            break;
        case Instruction::FPToUI:
        case Instruction::FPToSI:
            if (typeCanBePointer(&M->getDataLayout(), Inst.getType()))
                node = createCast(&Inst);
            else
                node = createUnknown(&Inst);
            break;
        case Instruction::InsertElement:
            return createInsertElement(&Inst);
        case Instruction::ExtractElement:
            return createExtractElement(&Inst);
        case Instruction::ShuffleVector:
            llvm::errs() << "ShuffleVector instruction is not supported, loosing precision\n";
            node = createUnknown(&Inst);
            break;
        default:
            llvm::errs() << Inst << "\n";
            assert(0 && "Unhandled instruction");
            node = createUnknown(&Inst);
    }

    return std::make_pair(node, node);
}

// is the instruction relevant to points-to analysis?
bool LLVMPointerGraphBuilder::isRelevantInstruction(const llvm::Instruction& Inst)
{
    using namespace llvm;

    switch(Inst.getOpcode()) {
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
PSNode *LLVMPointerGraphBuilder::createArgument(const llvm::Argument *farg)
{
    using namespace llvm;

    PSNode *arg = PS.create(PSNodeType::PHI, nullptr);
    addNode(farg, arg);

    return arg;
}

void LLVMPointerGraphBuilder::checkMemSet(const llvm::Instruction *Inst)
{
    using namespace llvm;

    bool zeroed = memsetIsZeroInitialization(cast<IntrinsicInst>(Inst));
    if (!zeroed) {
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
        if (tyContainsPointer(AI->getAllocatedType()))
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



// Get llvm BasicBlock's in levels of Dominator Tree (BFS order through the dominator tree)
std::vector<const llvm::BasicBlock *> getBasicBlocksInDominatorOrder(llvm::Function& F)
{
    std::vector<const llvm::BasicBlock *> blocks;
    blocks.reserve(F.size());

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9))
        llvm::DominatorTree DTree;
        DTree.recalculate(F);
#else
        llvm::DominatorTreeWrapperPass wrapper;
        wrapper.runOnFunction(F);
        auto& DTree = wrapper.getDomTree();
#ifndef NDEBUG
        wrapper.verifyAnalysis();
#endif
#endif

    auto root_node = DTree.getRootNode();
    blocks.push_back(root_node->getBlock());

    std::vector<llvm::DomTreeNode *> to_process;
    to_process.reserve(4);
    to_process.push_back(root_node);

    while (!to_process.empty()) {
        std::vector<llvm::DomTreeNode *> new_to_process;
        new_to_process.reserve(to_process.size());

        for (auto cur_node : to_process) {
            for (auto child : *cur_node) {
                new_to_process.push_back(child);
                blocks.push_back(child->getBlock());
            }
        }

        to_process.swap(new_to_process);
    }

    return blocks;
}

void LLVMPointerGraphBuilder::buildArguments(const llvm::Function& F,
                                             PointerSubgraph *parent)
{
    for (auto A = F.arg_begin(), E = F.arg_end(); A != E; ++A) {
#ifndef NDEBUG
        PSNode *a = tryGetOperand(&*A);
        // we must not have built this argument before
        // (or it is a number or irelevant value)
        assert(a == nullptr || a == UNKNOWN_MEMORY);
#endif
        auto arg = createArgument(&*A);
        arg->setParent(parent);
    }
}

PointerSubgraph&
LLVMPointerGraphBuilder::buildFunction(const llvm::Function& F)
{
    DBG_SECTION_BEGIN(pta, "building function '" << F.getName().str() << "'");

    assert(!getSubgraph(&F) && "We already built this function");
    assert(!F.isDeclaration() && "Cannot build an undefined function");

    // create root and later (an unified) return nodes of this subgraph.
    // These are just for our convenience when building the graph,
    // they can be optimized away later since they are noops
    PSNodeEntry *root = PSNodeEntry::get(PS.create(PSNodeType::ENTRY));
    assert(root);
    root->setFunctionName(F.getName().str());

    // if the function has variable arguments,
    // then create the node for it
    PSNode *vararg = nullptr;
    if (F.isVarArg()) {
        vararg = PS.create(PSNodeType::PHI, nullptr);
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
    auto& finfo = _funcInfo[&F];
    finfo.llvmBlocks =
        getBasicBlocksInDominatorOrder(const_cast<llvm::Function&>(F));

    // build the instructions from blocks
    for (const llvm::BasicBlock *block : finfo.llvmBlocks) {
        auto seq = buildPointerGraphBlock(*block, subg);

        // gather all return nodes
        if (seq.second &&
            (seq.second->getType() == PSNodeType::RETURN)) {
            subg->returnNodes.insert(seq.second);
        }
    }

    // add operands to PHI nodes. It must be done after all blocks are
    // built, since the PHI gathers values from different blocks
    addPHIOperands(F);

    assert(getSubgraph(&F)->root != nullptr);
    DBG_SECTION_END(pta, "building function '" << F.getName().str() << "' done");
    return *subg;
}

void LLVMPointerGraphBuilder::addProgramStructure()
{
    // form intraprocedural program structure (CFG edges)
    for (auto& it : subgraphs_map) {
        const llvm::Function *F = it.first;
        PointerSubgraph *subg = it.second;
        assert(subg && "Subgraph was nullptr");

        // add the CFG edges
        addProgramStructure(F, *subg);

        // add the missing operands (to arguments and return nodes)
        addInterproceduralOperands(F, *subg);
    }
}

PointerGraph *LLVMPointerGraphBuilder::buildLLVMPointerGraph()
{
    DBG_SECTION_BEGIN(pta, "building pointer graph");

    // get entry function
    llvm::Function *F = M->getFunction(_options.entryFunction);
    if (!F) {
        llvm::errs() << "Did not find " << _options.entryFunction << " function in module\n";
        abort();
    }

    // first we must build globals, because nodes can use them as operands
    buildGlobals();

    // now we can build rest of the graph
    PointerSubgraph& subg = buildFunction(*F);
    PSNode *root = subg.root;
    assert(root != nullptr);
    // fill in the CFG edges
    addProgramStructure();

    // FIXME: set entry procedure, not an entry node
    auto mainsg = getSubgraph(F);
    assert(mainsg);
    PS.setEntry(mainsg);

#ifndef NDEBUG
    for (const auto& subg : PS.getSubgraphs()) {
        assert(subg->root && "No root in a subgraph");
    }

    debug::LLVMPointerGraphValidator validator(&PS);
    if (validator.validate()) {
        llvm::errs() << validator.getWarnings();

        llvm::errs() << "Pointer Subgraph is broken (right after building)!\n";
        assert(!validator.getErrors().empty());
        llvm::errs() << validator.getErrors();
        //return nullptr;
    } else {
        llvm::errs() << validator.getWarnings();
    }
#endif // NDEBUG

    DBG_SECTION_END(pta, "building pointer graph done");

    return &PS;
}


bool LLVMPointerGraphBuilder::validateSubgraph(bool no_connectivity) const
{
    debug::LLVMPointerGraphValidator validator(getPS(), no_connectivity);
    if (validator.validate()) {
        assert(!validator.getErrors().empty());
        llvm::errs() << validator.getErrors();
        return false;
    } else {
        return true;
    }
}

std::vector<PSNode *>
LLVMPointerGraphBuilder::getFunctionNodes(const llvm::Function *F) const
{
    auto it = subgraphs_map.find(F);
    if (it == subgraphs_map.end())
        return {};

    auto nodes = getReachableNodes(it->second->root, nullptr, false /* interproc */);
    std::vector<PSNode *> ret;
    ret.reserve(nodes.size());
    std::copy(nodes.begin(), nodes.end(), std::back_inserter(ret));

    return ret;
}

} // namespace pta
} // namespace analysis
} // namespace dg
