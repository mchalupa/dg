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

#include "analysis/PointsTo/PointerSubgraph.h"
#include "llvm/analysis/PointsTo/PointerSubgraphValidator.h"
#include "analysis/PointsTo/PointerSubgraphOptimizations.h"
#include "PointerSubgraph.h"

namespace dg {
namespace analysis {
namespace pta {

using dg::MemAllocationFuncs;

LLVMPointerSubgraphBuilder::~LLVMPointerSubgraphBuilder()
{
    delete DL;
}

void dump(const llvm::BasicBlock& b) {
    llvm::errs() << b << "\n";
}

void dump(const llvm::Instruction& I) {
    llvm::errs() << I << "\n";
}

void dump(const llvm::Value& V) {
    llvm::errs() << V << "\n";
}

PSNode *LLVMPointerSubgraphBuilder::getConstant(const llvm::Value *val)
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
PSNode *LLVMPointerSubgraphBuilder::tryGetOperand(const llvm::Value *val)
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
    if (op->getType() == PSNodeType::CALL
        || op->getType() == PSNodeType::CALL_FUNCPTR) {
        op = op->getPairedNode();
    }

    return op;
}

PSNode *LLVMPointerSubgraphBuilder::getOperand(const llvm::Value *val)
{
    PSNode *op = tryGetOperand(val);
    if (!op) {
        if (isInvalid(val))
            return UNKNOWN_MEMORY;

        llvm::errs() << "ERROR: missing value in graph: " << *val << "\n";
        abort();
    } else
        return op;
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createCallToFunction(const llvm::CallInst *CInst, const llvm::Function *F)
{
    PSNodeCall *callNode;
    PSNode *returnNode;

    // the operands to the return node (which works as a phi node)
    // are going to be added when the subgraph is built
    callNode = PSNodeCall::get(PS.create(PSNodeType::CALL));
    returnNode = PS.create(PSNodeType::CALL_RETURN, nullptr);

    returnNode->setPairedNode(callNode);
    callNode->setPairedNode(returnNode);

    // reuse built subgraphs if available
    Subgraph& subg = createOrGetSubgraph(F);
    if (ad_hoc_building) {
        // add operands to arguments
        addInterproceduralOperands(F, subg, CInst);
    }

    // we took the subg by reference, so it should be filled now
    assert(subg.root && subg.ret);

    // add an edge from last argument to root of the subgraph
    // and from the subprocedure return node (which is one - unified
    // for all return nodes) to return from the call
    callNode->addSuccessor(subg.root);
    subg.ret->addSuccessor(returnNode);

    // handle value returned from the function if it is a pointer
    // DONT: if (CInst->getType()->isPointerTy()) {
    // we need to handle the return values even when it is not
    // a pointer as we have ptrtoint and inttoptr

    return std::make_pair(callNode, returnNode);
}

PSNodesSeq
LLVMPointerSubgraphBuilder::createFuncptrCall(const llvm::CallInst *CInst,
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
    Subgraph& subg = subgraphs_map[F];
    assert(subg.root != nullptr);
#endif

    ad_hoc_building = false;

    return ret;
}

LLVMPointerSubgraphBuilder::Subgraph&
LLVMPointerSubgraphBuilder::createOrGetSubgraph(const llvm::Function *F)
{
    auto it = subgraphs_map.find(F);
    if (it == subgraphs_map.end()) {
        // create a new subgraph
        Subgraph& subg = buildFunction(*F);
        assert(subg.root != nullptr);

        if (ad_hoc_building) {
            addProgramStructure(F, subg);
        }

        return subg;
    }

    return it->second;
}

void LLVMPointerSubgraphBuilder::addPHIOperands(PSNode *node, const llvm::PHINode *PHI)
{
    for (int i = 0, e = PHI->getNumIncomingValues(); i < e; ++i) {
        if (PSNode *op = tryGetOperand(PHI->getIncomingValue(i))) {
            // do not add duplicate operands
            if (!node->hasOperand(op))
                node->addOperand(op);
        }
    }
}

void LLVMPointerSubgraphBuilder::addPHIOperands(const llvm::Function &F)
{
    for (const llvm::BasicBlock& B : F) {
        for (const llvm::Instruction& I : B) {
            if (const llvm::PHINode *PHI = llvm::dyn_cast<llvm::PHINode>(&I)) {
                if (PSNode *node = getNode(PHI))
                    addPHIOperands(node, PHI);
            }
        }
    }
}

static bool isRelevantCall(const llvm::Instruction *Inst)
{
    using namespace llvm;

    // we don't care about debugging stuff
    if (isa<DbgValueInst>(Inst))
        return false;

    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();
    const Function *func = dyn_cast<Function>(calledVal);

    if (!func)
        // function pointer call - we need that in PointerSubgraph
        return true;

    if (func->size() == 0) {
        if (getMemAllocationFunc(func) != MemAllocationFuncs::NONEMEM)
            // we need memory allocations
            return true;

        if (func->getName().equals("free"))
            // we need calls of free
            return true;

        if (func->isIntrinsic())
            return isRelevantIntrinsic(func);

        // it returns something? We want that!
        return !func->getReturnType()->isVoidTy();
    } else
        // we want defined function, since those can contain
        // pointer's manipulation and modify CFG
        return true;

    assert(0 && "We should not reach this");
}

PSNodesSeq
LLVMPointerSubgraphBuilder::buildInstruction(const llvm::Instruction& Inst)
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
            if (typeCanBePointer(DL, Inst.getType()))
                node = createCast(&Inst);
            else
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
bool LLVMPointerSubgraphBuilder::isRelevantInstruction(const llvm::Instruction& Inst)
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
            return isRelevantCall(&Inst);
        default:
            return true;
    }

    assert(0 && "Not to be reached");
}

// create a formal argument
PSNode *LLVMPointerSubgraphBuilder::createArgument(const llvm::Argument *farg)
{
    using namespace llvm;

    PSNode *arg = PS.create(PSNodeType::PHI, nullptr);
    addNode(farg, arg);

    return arg;
}

void LLVMPointerSubgraphBuilder::checkMemSet(const llvm::Instruction *Inst)
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
            PSNodeAlloc::get(op)->setZeroInitialized();
    } else {
        // fallback: create a store that represents memset
        // the store will save null to ptr + Offset::UNKNOWN,
        // so we need to do:
        // G = GEP(op, Offset::UNKNOWN)
        // STORE(null, G)
        buildInstruction(*Inst);
    }
}

// return first and last nodes of the block
void LLVMPointerSubgraphBuilder::buildPointerSubgraphBlock(const llvm::BasicBlock& block)
{
    for (const llvm::Instruction& Inst : block) {
        if (!isRelevantInstruction(Inst)) {
            // check if it is a zeroing of memory,
            // if so, set the corresponding memory to zeroed
            if (llvm::isa<llvm::MemSetInst>(&Inst))
                checkMemSet(&Inst);

            continue;
        }

        assert(nodes_map.count(&Inst) == 0);

#ifndef NDEBUG
        PSNodesSeq seq =
#endif
        buildInstruction(Inst);

        assert(seq.first && seq.second
               && "Didn't created the instruction properly");
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

LLVMPointerSubgraphBuilder::Subgraph&
LLVMPointerSubgraphBuilder::buildFunction(const llvm::Function& F)
{
    assert(subgraphs_map.count(&F) == 0 && "We already built this function");

    // create root and (unified) return nodes of this subgraph. These are
    // just for our convenience when building the graph, they can be
    // optimized away later since they are noops
    // XXX: do we need entry type?
    PSNodeEntry *root = PSNodeEntry::get(PS.create(PSNodeType::ENTRY));
    assert(root);
    root->setFunctionName(F.getName().str());
    PSNode *ret;

    if (invalidate_nodes) {
        ret = PS.create(PSNodeType::INVALIDATE_LOCALS, root);
    } else {
        ret = PS.create(PSNodeType::NOOP);
    }

    // if the function has variable arguments,
    // then create the node for it
    PSNode *vararg = nullptr;
    if (F.isVarArg())
        vararg = PS.create(PSNodeType::PHI, nullptr);

    // create the pointer arguments -- the other arguments will
    // be created later if needed
    for (auto A = F.arg_begin(), E = F.arg_end(); A != E; ++A) {
#ifndef NDEBUG
        PSNode *a = tryGetOperand(&*A);
        assert(a == nullptr || a == UNKNOWN_MEMORY);
#endif
        createArgument(&*A);
    }

    // add record to built graphs here, so that subsequent call of this function
    // from buildPointerSubgraphBlock won't get stuck in infinite recursive call when
    // this function is recursive
    Subgraph subg(root, ret, vararg);
    auto it = subgraphs_map.emplace(&F, std::move(subg));
    assert(it.second == true && "Already had this element");

    Subgraph& s = it.first->second;
    assert(s.root == root && s.ret == ret && s.vararg == vararg);

    s.llvmBlocks =
        getBasicBlocksInDominatorOrder(const_cast<llvm::Function&>(F));

    // build the instructions from blocks
    for (const llvm::BasicBlock *block : s.llvmBlocks) {
        buildPointerSubgraphBlock(*block);
    }

    // add operands to PHI nodes. It must be done after all blocks are
    // built, since the PHI gathers values from different blocks
    addPHIOperands(F);

    assert(subgraphs_map[&F].root != nullptr);
    return s;
}

void LLVMPointerSubgraphBuilder::addProgramStructure()
{
    // form intraprocedural program structure (CFG edges)
    for (auto& it : subgraphs_map) {
        const llvm::Function *F = it.first;
        Subgraph& subg = it.second;

        // add the CFG edges
        addProgramStructure(F, subg);

        // if the return node has no predecessors,
        // then this function does not return.
        // Remove the return node.
        if (subg.ret->predecessorsNum() == 0) {
            subg.ret->isolate();
            PS.remove(subg.ret);
            subg.ret = nullptr;

            // remove also the CALL_RETURN node as it is
            // useles now
            for (PSNode *call : subg.root->getPredecessors()) {
                assert(call->getType() == PSNodeType::CALL ||
                       call->getType() == PSNodeType::CALL_FUNCPTR);
                PSNode *call_ret = call->getPairedNode();
                call->setPairedNode(nullptr);
                call_ret->isolate();
                PS.remove(call_ret);
            }
        }

        // add the missing operands (to arguments and return nodes)
        addInterproceduralOperands(F, subg);
    }
}

void LLVMPointerSubgraphBuilder::addArgumentOperands(const llvm::CallInst *CI,
                                                     PSNode *arg, int idx)
{
    assert(idx < static_cast<int>(CI->getNumArgOperands()));
    PSNode *op = tryGetOperand(CI->getArgOperand(idx));
    if (op && !arg->hasOperand(op)) {
        // NOTE: do not add an operand multiple-times
        // (when a function is called multiple-times with
        // the same actual parameters)
        arg->addOperand(op);
    }
}

void LLVMPointerSubgraphBuilder::addArgumentOperands(const llvm::Function *F,
                                                     PSNode *arg, int idx)
{
    using namespace llvm;

    for (auto I = F->use_begin(), E = F->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        const Value *use = *I;
#else
        const Value *use = I->getUser();
#endif
        const CallInst *CI = dyn_cast<CallInst>(use);
        if (CI && CI->getCalledFunction() == F)
            addArgumentOperands(CI, arg, idx);
    }
}

void LLVMPointerSubgraphBuilder::addArgumentsOperands(const llvm::Function *F,
                                                      const llvm::CallInst *CI)
{
    int idx = 0;
    for (auto A = F->arg_begin(), E = F->arg_end(); A != E; ++A, ++idx) {
        auto it = nodes_map.find(&*A);
        assert(it != nodes_map.end());

        PSNodesSeq& cur = it->second;
        assert(cur.first == cur.second);

        if (CI)
            // with func ptr call we know from which
            // call we should take the values
            addArgumentOperands(CI, cur.first, idx);
        else
            // with regular call just use all calls
            addArgumentOperands(F, cur.first, idx);
    }
}

void LLVMPointerSubgraphBuilder::addVariadicArgumentOperands(const llvm::Function *F,
                                                             const llvm::CallInst *CI,
                                                             PSNode *arg)
{
    for (unsigned idx = F->arg_size() - 1; idx < CI->getNumArgOperands(); ++idx)
        addArgumentOperands(CI, arg, idx);
}

void LLVMPointerSubgraphBuilder::addVariadicArgumentOperands(const llvm::Function *F,
                                                             PSNode *arg)
{
    using namespace llvm;

    for (auto I = F->use_begin(), E = F->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        const Value *use = *I;
#else
        const Value *use = I->getUser();
#endif
        const CallInst *CI = dyn_cast<CallInst>(use);
        if (CI && CI->getCalledFunction() == F)
            addVariadicArgumentOperands(F, CI, arg);
        // if this is funcptr, we handle it in the other
        // version of addVariadicArgumentOperands
    }
}

void LLVMPointerSubgraphBuilder::addReturnNodeOperands(const llvm::Function *F,
                                                       PSNode *ret,
                                                       const llvm::CallInst *CI)
{
    using namespace llvm;

    for (PSNode *r : ret->getPredecessors()) {
        // return node is like a PHI node,
        // we must add the operands too.
        // But we're interested only in the nodes that return some value
        // from subprocedure, not for all nodes that have no successor
        if (r->getType() == PSNodeType::RETURN) {
            if (CI)
                addReturnNodeOperand(CI, r);
            else
                addReturnNodeOperand(F, r);
        }
    }
}

void LLVMPointerSubgraphBuilder::addReturnNodeOperand(const llvm::CallInst *CI, PSNode *op)
{
    PSNode *callNode = getNode(CI);
    // since we're building the graph from main and only where we can reach it,
    // we may not have all call-sites of a function
    if (!callNode)
        return;

    PSNode *returnNode = callNode->getPairedNode();
    // the function must be defined, since we have the return node,
    // so there must be associated the return node
    assert(returnNode);

    if (!returnNode->hasOperand(op))
        returnNode->addOperand(op);
}


void LLVMPointerSubgraphBuilder::addReturnNodeOperand(const llvm::Function *F, PSNode *op)
{
    using namespace llvm;

    for (auto I = F->use_begin(), E = F->use_end(); I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        const Value *use = *I;
#else
        const Value *use = I->getUser();
#endif
        // get every call and its assocciated return and add the operand
        const CallInst *CI = dyn_cast<CallInst>(use);
        if (CI && CI->getCalledFunction() == F)
            addReturnNodeOperand(CI, op);
    }
}

void LLVMPointerSubgraphBuilder::addInterproceduralOperands(const llvm::Function *F,
                                                            Subgraph& subg,
                                                            const llvm::CallInst *CI)
{
    // add operands to arguments' PHI nodes
    addArgumentsOperands(F, CI);

    if (F->isVarArg()) {
        assert(subg.vararg);
        if (CI)
            // funcptr call
            addVariadicArgumentOperands(F, CI, subg.vararg);
        else
            addVariadicArgumentOperands(F, subg.vararg);
    }

    if (subg.ret) {
        addReturnNodeOperands(F, subg.ret, CI);
    }
}


PointerSubgraph *LLVMPointerSubgraphBuilder::buildLLVMPointerSubgraph()
{
    // get entry function
    llvm::Function *F = M->getFunction("main");
    if (!F) {
        llvm::errs() << "Need main function in module\n";
        abort();
    }

    // first we must build globals, because nodes can use them as operands
    PSNodesSeq glob = buildGlobals();

    // now we can build rest of the graph
    Subgraph& subg = buildFunction(*F);
    PSNode *root = subg.root;
    assert(root != nullptr);

    // fill in the CFG edges
    addProgramStructure();

    // do we have any globals at all? If so, insert them at the begining
    // of the graph
    // FIXME: we do not need to process them later,
    // should we do it somehow differently?
    // something like 'static nodes' in PointerSubgraph...
    if (glob.first) {
        assert(glob.second && "Have the start but not the end");

        // this is a sequence of global nodes, make it the root of the graph
        glob.second->addSuccessor(root);
        root = glob.first;
    }

    PS.setRoot(root);

#ifndef NDEBUG
    debug::LLVMPointerSubgraphValidator validator(&PS);
    if (validator.validate()) {
        llvm::errs() << validator.getWarnings();

        llvm::errs() << "Pointer Subgraph is broken (right after building)!\n";
        assert(!validator.getErrors().empty());
        llvm::errs() << validator.getErrors();
        return nullptr;
    } else {
        llvm::errs() << validator.getWarnings();
    }
#endif // NDEBUG

    return &PS;
}

} // namespace pta
} // namespace analysis
} // namespace dg
