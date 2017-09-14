#include <set>
#include <cassert>

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

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif


#include "analysis/PointsTo/PointerSubgraph.h"
#include "llvm/analysis/PointsTo/PointerSubgraph.h"
#include "llvm/llvm-utils.h"
#include "ReachingDefinitions.h"

namespace dg {
namespace analysis {
namespace rd {

static uint64_t getAllocatedSize(llvm::Type *Ty, const llvm::DataLayout *DL)
{
    // Type can be i8 *null or similar
    if (!Ty->isSized())
            return 0;

    return DL->getTypeAllocSize(Ty);
}

// FIXME: don't duplicate the code (with PSS.cpp)
static uint64_t getConstantValue(const llvm::Value *op)
{
    using namespace llvm;

    uint64_t size = 0;
    if (const ConstantInt *C = dyn_cast<ConstantInt>(op)) {
        size = C->getLimitedValue();
        // if the size cannot be expressed as an uint64_t,
        // just set it to 0 (that means unknown)
        if (size == ~((uint64_t) 0))
            size = 0;
    }

    return size;
}

LLVMRDBuilder::~LLVMRDBuilder() {
    // delete data layout
    delete DL;

    // delete artificial nodes from subgraphs
    for (auto& it : subgraphs_map) {
        assert((it.second.root && it.second.ret) ||
               (!it.second.root && !it.second.ret));
        delete it.second.root;
        delete it.second.ret;
    }

    // delete nodes
    for (auto& it : nodes_map) {
        assert(it.first && "Have a nullptr node mapping");
        delete it.second;
    }

    // delete dummy nodes
    for (RDNode *nd : dummy_nodes)
        delete nd;
}

static uint64_t getAllocatedSize(const llvm::AllocaInst *AI,
                                 const llvm::DataLayout *DL)
{
    llvm::Type *Ty = AI->getAllocatedType();
    if (!Ty->isSized())
            return 0;

    if (AI->isArrayAllocation())
        return getConstantValue(AI->getArraySize()) * DL->getTypeAllocSize(Ty);
    else
        return DL->getTypeAllocSize(Ty);
}

RDNode *LLVMRDBuilder::createAlloc(const llvm::Instruction *Inst, RDBlock *rb)
{
    RDNode *node = new RDNode(RDNodeType::ALLOC);
    addNode(Inst, node);
    rb->append(node);

    if (const llvm::AllocaInst *AI
            = llvm::dyn_cast<llvm::AllocaInst>(Inst))
        node->setSize(getAllocatedSize(AI, DL));

    return node;
}

RDNode *LLVMRDBuilder::createDynAlloc(const llvm::Instruction *Inst, MemAllocationFuncs type, RDBlock *rb)
{
    using namespace llvm;

    RDNode *node = new RDNode(RDNodeType::DYN_ALLOC);
    addNode(Inst, node);
    rb->append(node);

    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *op;
    uint64_t size = 0, size2 = 0;

    switch (type) {
        case MemAllocationFuncs::MALLOC:
        case MemAllocationFuncs::ALLOCA:
            op = CInst->getOperand(0);
            break;
        case MemAllocationFuncs::CALLOC:
            op = CInst->getOperand(1);
            break;
        default:
            errs() << *CInst << "\n";
            assert(0 && "unknown memory allocation type");
            // for NDEBUG
            abort();
    };

    // infer allocated size
    size = getConstantValue(op);
    if (size != 0 && type == MemAllocationFuncs::CALLOC) {
        // if this is call to calloc, the size is given
        // in the first argument too
        size2 = getConstantValue(CInst->getOperand(0));
        if (size2 != 0)
            size *= size2;
    }

    node->setSize(size);
    return node;
}

RDNode *LLVMRDBuilder::createRealloc(const llvm::Instruction *Inst, RDBlock *rb)
{
    RDNode *node = new RDNode(RDNodeType::DYN_ALLOC);
    addNode(Inst, node);
    rb->append(node);

    uint64_t size = getConstantValue(Inst->getOperand(1));
    if (size == 0)
        size = UNKNOWN_OFFSET;
    else
        node->setSize(size);

    // realloc defines itself, since it copies the values
    // from previous memory
    node->addDef(node, 0, size, false /* strong update */);

    return node;
}

static void getLocalVariables(const llvm::Function *F,
                              std::set<const llvm::Value *>& ret)
{
    using namespace llvm;

    // get all alloca insts that are not address taken
    // (are not stored into a pointer)
    // -- that means that they can not be used outside of
    // this function
    for (const BasicBlock& block : *F) {
        for (const Instruction& Inst : block) {
            if (isa<AllocaInst>(&Inst)) {
                bool is_address_taken = false;
                for (auto I = Inst.use_begin(), E = Inst.use_end();
                     I != E; ++I) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
                    const llvm::Value *use = *I;
#else
                    const llvm::Value *use = I->getUser();
#endif
                    const StoreInst *SI = dyn_cast<StoreInst>(use);
                    // is the value operand our alloca?
                    if (SI && SI->getValueOperand() == &Inst) {
                        is_address_taken = true;
                        break;
                    }
                }

                if (!is_address_taken)
                    ret.insert(&Inst);
            }
        }
    }
}

RDNode *LLVMRDBuilder::createReturn(const llvm::Instruction *Inst, RDBlock *rb)
{
    RDNode *node = new RDNode(RDNodeType::RETURN);
    addNode(Inst, node);
    rb->append(node);

    // FIXME: don't do that for every return instruction,
    // compute it only once for a function
    std::set<const llvm::Value *> locals;
    getLocalVariables(Inst->getParent()->getParent(),
                      locals);

    for (const llvm::Value *ptrVal : locals) {
        RDNode *ptrNode = getOperand(ptrVal, rb);
        if (!ptrNode) {
            llvm::errs() << *ptrVal << "\n";
            llvm::errs() << "Don't have created node for local variable\n";
            abort();
        }

        // make this return node behave like we overwrite the definitions.
        // We actually don't override them, therefore they are dropped
        // and that is what we want (we don't want to propagade
        // local definitions from functions into callees)
        node->addOverwrites(ptrNode, 0, UNKNOWN_OFFSET);
    }

    return node;
}

RDNode *LLVMRDBuilder::getOperand(const llvm::Value *val, RDBlock *rb)
{
    RDNode *op = getNode(val);
    if (!op)
        return createNode(*llvm::cast<llvm::Instruction>(val), rb);

    return op;
}

RDNode *LLVMRDBuilder::createNode(const llvm::Instruction &Inst, RDBlock *rb)
{
    using namespace llvm;

    RDNode *node = nullptr;
    switch(Inst.getOpcode()) {
        case Instruction::Alloca:
            // we need alloca's as target to DefSites
            node = createAlloc(&Inst, rb);
            break;
        case Instruction::Call:
            node = createCall(&Inst, rb).second;
            break;
        default:
            llvm::errs() << "BUG: " << Inst << "\n";
            abort();
    }

    return node;
}

static void operandDump(const llvm::Instruction *inst, RDNode *)
{
    printf("%s instruction %p: Operand Count: %u\t\n", inst->getOpcodeName(), inst, inst->getNumOperands());
    for (size_t i = 0; i < inst->getNumOperands(); ++i) {
        printf("\t%lu: %p\n", i, inst->getOperand(i));
    }
}

std::vector<DefSite> LLVMRDBuilder::getPointsTo(const llvm::Value *val, RDBlock *rb)
{
    std::vector<DefSite> result;

    pta::PSNode *psn = PTA->getPointsTo(val);

    if (!psn)
        // don't have points-to information for used pointer
        return result;

    if (psn->pointsTo.empty()) {
        result.push_back(DefSite(UNKNOWN_MEMORY));
    }

    for (const pta::Pointer& ptr : psn->pointsTo) {
        if (ptr.isNull())
            continue;

        if (ptr.isUnknown()) {
            result.push_back(DefSite(UNKNOWN_MEMORY));
            continue;
        }

        const llvm::Value *ptrVal = ptr.target->getUserData<llvm::Value>();
        if (llvm::isa<llvm::Function>(ptrVal))
            continue;

        RDNode *ptrNode = getOperand(ptrVal, rb);
        if (!ptrNode) {
            // XXX: maybe warn here
            llvm::errs() << *ptrVal << "\n";
            llvm::errs() << "Don't have created node for pointer's source\n";
            continue;
        }

        uint64_t size;
        if (ptr.offset.isUnknown()) {
            size = UNKNOWN_OFFSET;
        } else {
            size = getAllocatedSize(val->getType(), DL);
            if (size == 0)
                size = UNKNOWN_OFFSET;
        }

        // TODO: fix strong updates
        /* bool strong_update = psn->pointsTo.size()  == 1 && !psn->isHeap(); */
        result.push_back(DefSite(ptrNode, ptr.offset, size));
    }
    return result;
}

RDNode *LLVMRDBuilder::createLoad(const llvm::Instruction *Inst, RDBlock *rb)
{
    RDNode *node = new RDNode(RDNodeType::LOAD);
    addNode(Inst, node);
    rb->append(node);

    node->addUses(getPointsTo(Inst->getOperand(0), rb));

    return node;
}

RDNode *LLVMRDBuilder::createStore(const llvm::Instruction *Inst, RDBlock *rb)
{
    RDNode *node = new RDNode(RDNodeType::STORE);
    addNode(Inst, node);
    rb->append(node);

    // check if argument 0 is a pointer
    llvm::Value *val = Inst->getOperand(0);
    if (val->getType()->isPointerTy()) {
        node->addUses(getPointsTo(val, rb));
    }

    node->addDefs(getPointsTo(Inst->getOperand(1), rb));

    assert(node);
    return node;
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
        // function pointer call - we need that
        return true;

    if (func->size() == 0) {
        if (getMemAllocationFunc(func) != MemAllocationFuncs::NONEMEM)
            // we need memory allocations
            return true;

        if (func->isIntrinsic()) {
            switch (func->getIntrinsicID()) {
                case Intrinsic::memmove:
                case Intrinsic::memcpy:
                case Intrinsic::memset:
                case Intrinsic::vastart:
                    return true;
                default:
                    return false;
            }
        }

        // undefined function
        return true;
    } else
        // we want defined function, since those can contain
        // pointer's manipulation and modify CFG
        return true;

    assert(0 && "We should not reach this");
}

/**
 * returns all RDBlock-s to which the LLVM block maps.
 */
std::vector<RDBlock *>
LLVMRDBuilder::buildBlock(const llvm::BasicBlock& block)
{
    using namespace llvm;

    std::vector<RDBlock *> result;
    RDBlock *rb = new RDBlock();
    rb->setKey(const_cast<llvm::BasicBlock *>(&block));
    addBlock(&block, rb);
    result.push_back(rb);

    // the first node is dummy and serves as a phi from previous
    // blocks so that we can have proper mapping
    RDNode *node = new RDNode(RDNodeType::PHI);
    RDNode *last_node = node;

    addNode(node);
    rb->append(node);

    for (const Instruction& Inst : block) {
        node = getNode(&Inst);
        if (!node) {
            RDBlock *new_block = nullptr;
            switch(Inst.getOpcode()) {
                case Instruction::Alloca:
                    // we need alloca's as target to DefSites
                    node = createAlloc(&Inst, rb);
                    break;
                case Instruction::Store:
                    node = createStore(&Inst, rb);
                    break;
                case Instruction::Ret:
                    // we need create returns, since
                    // these modify CFG and thus data-flow
                    // FIXME: add new type of node NOOP,
                    // and optimize it away later
                    node = createReturn(&Inst, rb);
                    break;
                case Instruction::Load:
                    node = createLoad(&Inst, rb);
                    break;
                case Instruction::Call:
                    if (!isRelevantCall(&Inst))
                        break;

                    std::pair<RDNode *, RDNode *> subg = createCall(&Inst, rb);
                    last_node->addSuccessor(subg.first);
                    if (subg.first->successorsNum() > 0) {
                        rb->addSuccessor((*subg.first->getSuccessors().begin())->getBBlock());

                        // successors for blocks with return will be added later
                        new_block = new RDBlock();
                        addBlock(const_cast<llvm::BasicBlock *>(&block), new_block);
                        result.push_back(new_block);
                        new_block->append(subg.second);
                        rb = new_block;

                        (*subg.second->getPredecessors().begin())->getBBlock()->addSuccessor(new_block);
                    }

                    // new nodes will connect to the return node
                    node = last_node = subg.second;
                    break;
            }
        }

        // last_node should never be null
        assert(last_node != nullptr && "BUG: Last node is null");

        // we either created a new node or reused some old node,
        // or node is nullptr (if we haven't created or found anything)
        // if we created a new node, add successor
        if (node) {
            last_node->addSuccessor(node);
            last_node = node;
        }

        // reaching definitions for this Inst are contained
        // in the last created node
        addMapping(&Inst, last_node);
    }

    return result;
}

static size_t blockAddSuccessors(std::map<const llvm::BasicBlock *,
                                          std::vector<RDBlock *>>& built_blocks,
                                 RDBlock *ptan,
                                 const llvm::BasicBlock& block)
{
    size_t num = 0;

    for (llvm::succ_const_iterator
         S = llvm::succ_begin(&block), SE = llvm::succ_end(&block); S != SE; ++S) {
        RDBlock *succ = *(built_blocks[*S].begin());
        assert((succ->getFirstNode() && succ->getLastNode()) || (!succ->getFirstNode() && !succ->getLastNode()));
        if (!succ->getFirstNode()) {
            // if we don't have this block built (there was no points-to
            // relevant instruction), we must pretend to be there for
            // control flow information. Thus instead of adding it as
            // successor, add its successors as successors
            num += blockAddSuccessors(built_blocks, ptan, *(*S));
        } else {
            // add successor to the last nodes
            ptan->getLastNode()->addSuccessor(succ->getFirstNode());
            ptan->addSuccessor(succ);
            ++num;
        }
    }

    return num;
}

std::pair<RDNode *, RDNode *>
LLVMRDBuilder::createCallToFunction(const llvm::Function *F, RDBlock *rb)
{
    RDNode *callNode, *returnNode;

    // dummy nodes for easy generation
    callNode = new RDNode(RDNodeType::CALL);
    returnNode = new RDNode(RDNodeType::CALL_RETURN);

    // do not leak the memory of returnNode (the callNode
    // will be added to nodes_map)
    addNode(returnNode);
    rb->append(callNode);

    // FIXME: if this is an inline assembly call
    // we need to make conservative assumptions
    // about that - assume that every pointer
    // passed to the subprocesdure may be defined on
    // UNKNOWN OFFSET, etc.

    // reuse built subgraphs if available, so that we won't get
    // stuck in infinite loop with recursive functions
    RDNode *root, *ret;
    auto it = subgraphs_map.find(F);
    if (it == subgraphs_map.end()) {
        // create a new subgraph
        RDBlock *first, *last;
        std::tie(first, last) = buildFunction(*F);
        root = first->getFirstNode();
        ret = last->getLastNode();
    } else {
        root = it->second.root;
        ret = it->second.ret;
    }

    assert(root && ret && "Incomplete subgraph");

    // add an edge from last argument to root of the subgraph
    // and from the subprocedure return node (which is one - unified
    // for all return nodes) to return from the call
    callNode->addSuccessor(root);
    ret->addSuccessor(returnNode);

    return std::make_pair(callNode, returnNode);
}

std::pair<RDBlock *, RDBlock *>
LLVMRDBuilder::buildFunction(const llvm::Function& F)
{
    // here we'll keep first and last nodes of every built block and
    // connected together according to successors
    std::map<const llvm::BasicBlock *, std::vector<RDBlock *>> built_blocks;

    // create root and (unified) return nodes of this subgraph. These are
    // just for our convenience when building the graph, they can be
    // optimized away later since they are noops
    RDNode *root = new RDNode(RDNodeType::NOOP);
    RDNode *ret = new RDNode(RDNodeType::NOOP);

    // emplace new subgraph to avoid looping with recursive functions
    subgraphs_map.emplace(&F, Subgraph(root, ret));

    RDNode *first = nullptr;
    RDBlock *fstblock = nullptr;
    RDBlock *lastblock = nullptr;

    RDBlock *prev_bblock_end = nullptr;
    for (const llvm::BasicBlock& block : F) {
        std::vector<RDBlock *> blocks = buildBlock(block);
        if (prev_bblock_end) {
            // connect the blocks within function
            prev_bblock_end->addSuccessor(blocks[0]);
        }

        if (!first) {
            first = blocks[0]->getFirstNode();
            fstblock = blocks[0];
        }
        prev_bblock_end = blocks.back();

        built_blocks[&block] = std::move(blocks);
    }

    assert(first);
    fstblock->prepend(root);
    root->addSuccessor(first);

    std::vector<RDNode *> rets;
    const llvm::BasicBlock *last_llvm_block = nullptr;
    RDBlock *last_function_block = nullptr;
    for (const llvm::BasicBlock& block : F) {
        auto it = built_blocks.find(&block);
        if (it == built_blocks.end())
            continue;

        for (RDBlock *ptan : it->second) {
            // assert((ptan.first && ptan.second) || (!ptan.first && !ptan.second));
            if (!ptan->getFirstNode())
                continue;

            // if we have not added any successor, then the last node
            // of this block is a return node
            if (ptan->getLastNode()->getType() == RDNodeType::RETURN)
                rets.push_back(ptan->getLastNode());
            last_function_block = it->second.back();
        }
        last_llvm_block = &block;
        // add successors to this block (skipping the empty blocks)
        // FIXME: this function is shared with PSS, factor it out
        size_t succ_num = blockAddSuccessors(built_blocks, last_function_block, *last_llvm_block);
    }

    // add successors edges from every real return to our artificial ret node
    last_function_block->append(ret);
    for (RDNode *r : rets) {
        r->addSuccessor(ret);
        if (r->getBBlock() != last_function_block)
            r->getBBlock()->addSuccessor(last_function_block);
    }

    functions_blocks[&F] = std::move(built_blocks);
    return std::make_pair(fstblock, last_function_block);
}

RDNode *LLVMRDBuilder::createUndefinedCall(const llvm::CallInst *CInst, RDBlock *rb)
{
    using namespace llvm;

    RDNode *node = new RDNode(RDNodeType::CALL);
    addNode(CInst, node);
    rb->append(node);


    // every pointer we pass into the undefined call may be defined
    // in the function
    for (unsigned int i = 0; i < CInst->getNumArgOperands(); ++i) {
        const Value *llvmOp = CInst->getArgOperand(i);

        // constants cannot be redefined except for global variables
        // (that are constant, but may point to non constant memory
        const Value *strippedValue = llvmOp->stripPointerCasts();
        if (isa<Constant>(strippedValue)) {
            const GlobalVariable *GV = dyn_cast<GlobalVariable>(strippedValue);
            // if the constant is not global variable,
            // or the global variable points to constant memory
            if (!GV || GV->isConstant())
                continue;
        }

        pta::PSNode *pts = PTA->getPointsTo(llvmOp);
        // if we do not have a pts, this is not pointer
        // relevant instruction. We must do it this way
        // instead of type checking, due to the inttoptr.
        if (!pts)
            continue;

        for (const pta::Pointer& ptr : pts->pointsTo) {
            if (!ptr.isValid())
                continue;

            const llvm::Value *ptrVal = ptr.target->getUserData<llvm::Value>();
            if (llvm::isa<llvm::Function>(ptrVal))
                // function may not be redefined
                continue;

            RDNode *target = getOperand(ptrVal, rb);
            assert(target && "Don't have pointer target for call argument");

            // this call may define or use this memory
            if (!assume_pure_functions)
                node->addDef(target, UNKNOWN_OFFSET, UNKNOWN_OFFSET);
            node->addUse(DefSite(target, UNKNOWN_OFFSET, UNKNOWN_OFFSET));
        }
    }

    // XXX: to be completely correct, we should assume also modification
    // of all global variables, so we should perform a write to
    // unknown memory instead of the loop above

    return node;
}

RDNode *LLVMRDBuilder::createIntrinsicCall(const llvm::CallInst *CInst, RDBlock *rb)
{
    using namespace llvm;

    const IntrinsicInst *I = cast<IntrinsicInst>(CInst);
    const Value *dest;
    const Value *lenVal;

    RDNode *ret = new RDNode(RDNodeType::CALL);
    addNode(CInst, ret);
    rb->append(ret);

    switch (I->getIntrinsicID())
    {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
        case Intrinsic::memset:
            // memcpy/set <dest>, <src/val>, <len>
            dest = I->getOperand(0);
            lenVal = I->getOperand(2);
            break;
        case Intrinsic::vastart:
            // we create this node because this nodes works
            // as ALLOC in points-to, so we can have
            // reaching definitions to that
            ret = new RDNode(RDNodeType::CALL);
            ret->addDef(ret, 0, UNKNOWN_OFFSET);
            addNode(CInst, ret);
            return ret;
        default:
            return createUndefinedCall(CInst, rb);
    }

    ret = new RDNode(RDNodeType::CALL);
    addNode(CInst, ret);

    pta::PSNode *pts = PTA->getPointsTo(dest);
    assert(pts && "No points-to information");

    uint64_t len = UNKNOWN_OFFSET;
    if (const ConstantInt *C = dyn_cast<ConstantInt>(lenVal))
        len = C->getLimitedValue();

    for (const pta::Pointer& ptr : pts->pointsTo) {
        if (!ptr.isValid())
            continue;

        const llvm::Value *ptrVal = ptr.target->getUserData<llvm::Value>();
        if (llvm::isa<llvm::Function>(ptrVal))
            continue;

        uint64_t from, to;
        if (ptr.offset.isUnknown()) {
            // if the offset is UNKNOWN, use whole memory
            from = UNKNOWN_OFFSET;
            len = UNKNOWN_OFFSET;
        } else {
            from = *ptr.offset;
        }

        // do not allow overflow
        if (UNKNOWN_OFFSET - from > len)
            to = from + len;
        else
            to = UNKNOWN_OFFSET;

        RDNode *target = getOperand(ptrVal, rb);
        assert(target && "Don't have pointer target for intrinsic call");

        // add the definition
        ret->addDef(target, from, to, true /* strong update */);
    }

    return ret;
}

std::pair<RDNode *, RDNode *>
LLVMRDBuilder::createCall(const llvm::Instruction *Inst, RDBlock *rb)
{
    using namespace llvm;
    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *calledVal = CInst->getCalledValue()->stripPointerCasts();
    static bool warned_inline_assembly = false;

    if (CInst->isInlineAsm()) {
        if (!warned_inline_assembly) {
            llvm::errs() << "WARNING: RD: Inline assembler found\n";
            warned_inline_assembly = true;
        }

        RDNode *n = createUndefinedCall(CInst, rb);
        return std::make_pair(n, n);
    }

    const Function *func = dyn_cast<Function>(calledVal);
    if (func) {
        if (func->size() == 0) {
            RDNode *n;
            if (func->isIntrinsic()) {
                n = createIntrinsicCall(CInst, rb);
            } else {
                MemAllocationFuncs type = getMemAllocationFunc(func);
                if (type != MemAllocationFuncs::NONEMEM) {
                    if (type == MemAllocationFuncs::REALLOC)
                        n = createRealloc(CInst, rb);
                    else
                        n = createDynAlloc(CInst, type, rb);
                } else {
                    n = createUndefinedCall(CInst, rb);
                }
            }

            return std::make_pair(n, n);
        } else {
            std::pair<RDNode *, RDNode *> cf
                = createCallToFunction(func, rb);
            addNode(CInst, cf.first);
            return cf;
        }
    } else {
        // function pointer call
        pta::PSNode *op = PTA->getPointsTo(calledVal);
        assert(op && "Don't have points-to information");
        //assert(!op->pointsTo.empty() && "Don't have pointer to the func");
        if (op->pointsTo.empty()) {
            llvm::errs() << "WARNING: a call via a function pointer, but the points-to is empty\n"
                         << *CInst << "\n";
            RDNode *n = createUndefinedCall(CInst, rb);
            return std::make_pair(n, n);
        }

        RDNode *call_funcptr = nullptr, *ret_call = nullptr;

        if (op->pointsTo.size() > 1) {
            for (const pta::Pointer& ptr : op->pointsTo) {
                if (!ptr.isValid())
                    continue;

                // check if it is a function (varargs may
                // introduce some unprecision to func. pointers)
                if (!isa<Function>(ptr.target->getUserData<Value>()))
                    continue;

                const Function *F = ptr.target->getUserData<Function>();
                if (F->size() == 0) {
                    // the function is a declaration only,
                    // there's nothing better we can do
                    RDNode *n = createUndefinedCall(CInst, rb);
                    return std::make_pair(n, n);
                }

                // FIXME: these checks are repeated here, in PSSBuilder
                // and in LLVMDependenceGraph, we should factor them
                // out into a function...
                if (!llvmutils::callIsCompatible(F, CInst))
                    continue;

                std::pair<RDNode *, RDNode *> cf
                    = createCallToFunction(F, rb);
                addNode(cf.first);

                // connect the graphs
                if (!call_funcptr) {
                    assert(!ret_call);

                    // create the new nodes lazily
                    call_funcptr = new RDNode(RDNodeType::CALL);
                    ret_call = new RDNode(RDNodeType::CALL_RETURN);
                    addNode(CInst, call_funcptr);
                    addNode(ret_call);
                }

                call_funcptr->addSuccessor(cf.first);
                cf.second->addSuccessor(ret_call);
            }
        } else {
            // don't add redundant nodes if not needed
            const pta::Pointer& ptr = *(op->pointsTo.begin());
            if (ptr.isValid()) {
                const llvm::Value *valF = ptr.target->getUserData<llvm::Value>();
                if (const llvm::Function *F = llvm::dyn_cast<llvm::Function>(valF)) {
                    if (F->size() == 0) {
                        RDNode *n = createUndefinedCall(CInst, rb);
                        for (unsigned i = 0; i < CInst->getNumArgOperands(); ++i) {
                            if (CInst->getArgOperand(i)->getType()->isPointerTy())
                                n->addUses(getPointsTo(CInst->getArgOperand(i), rb));
                        }
                        return std::make_pair(n, n);
                    } else if (llvmutils::callIsCompatible(F, CInst)) {
                        std::pair<RDNode *, RDNode *> cf = createCallToFunction(F, rb);
                        addNode(cf.first);

                        call_funcptr = cf.first;
                        ret_call = cf.second;
                    }
                }
            }
        }

        if (!ret_call) {
            assert(!call_funcptr);
            llvm::errs() << "Function pointer call with no compatible pointer: "
                         << *CInst << "\n";

            RDNode *n = createUndefinedCall(CInst, rb);
            for (unsigned i = 0; i < CInst->getNumArgOperands(); ++i) {
                if (CInst->getArgOperand(i)->getType()->isPointerTy())
                    n->addUses(getPointsTo(CInst->getArgOperand(i), rb));
            }
            return std::make_pair(n, n);
        }

        assert(call_funcptr && ret_call);
        for (unsigned i = 0; i < CInst->getNumArgOperands(); ++i) {
            if (CInst->getArgOperand(i)->getType()->isPointerTy())
                call_funcptr->addUses(getPointsTo(CInst->getArgOperand(i), rb));
        }
        return std::make_pair(call_funcptr, ret_call);
    }
}

RDNode *LLVMRDBuilder::build()
{
    // get entry function
    llvm::Function *F = M->getFunction("main");
    if (!F) {
        llvm::errs() << "Need main function in module\n";
        abort();
    }

    // first we must build globals, because nodes can use them as operands
    RDBlock *glob = buildGlobals();

    // now we can build rest of the graph
    RDBlock *start, *stop;
    std::tie(start, stop) = buildFunction(*F);
    RDNode *root = start->getFirstNode();
    assert(root);

    // do we have any globals at all? If so, insert them at the begining
    // of the graph
    if (glob->getFirstNode()) {
        assert(glob->getLastNode() && "Have the start but not the end");
        llvm::Value *data = glob->getFirstNode()->getUserData<llvm::Value>();
        addBlock(data, glob);

        // this is a sequence of global nodes, make it the root of the graph
        glob->addSuccessor(start);
        glob->getLastNode()->addSuccessor(root);

        assert(root->successorsNum() > 0);
        root = glob->getFirstNode();
    } else {
        delete glob;
    }

    return root;
}

RDBlock *LLVMRDBuilder::buildGlobals()
{
    RDBlock *glob = new RDBlock();
    RDNode *cur = nullptr, *prev, *first = nullptr;
    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        prev = cur;

        // every global node is like memory allocation
        cur = new RDNode(RDNodeType::ALLOC);
        addNode(&*I, cur);
        glob->append(cur);

        if (prev)
            prev->addSuccessor(cur);
        else
            first = cur;
    }

    assert((!first && !cur) || (first && cur));
    return glob;
}

} // namespace rd
} // namespace analysis
} // namespace dg
