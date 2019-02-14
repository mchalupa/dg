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


#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "dg/llvm/analysis/PointsTo/PointerSubgraph.h"
#include "llvm/analysis/ReachingDefinitions/LLVMRDBuilderSemisparse.h"
#include "llvm/llvm-utils.h"

namespace dg {
namespace analysis {
namespace rd {

RDNode *LLVMRDBuilderSemisparse::createAlloc(const llvm::Instruction *Inst, RDBlock *rb)
{
    RDNode *node = new RDNode(RDNodeType::ALLOC);
    addNode(Inst, node);
    rb->append(node);

    if (const llvm::AllocaInst *AI
            = llvm::dyn_cast<llvm::AllocaInst>(Inst))
        node->setSize(getAllocatedSize(AI, DL));

    return node;
}

RDNode *LLVMRDBuilderSemisparse::createDynAlloc(const llvm::Instruction *Inst,
                                                AllocationFunction type, RDBlock *rb)
{
    using namespace llvm;

    RDNode *node = new RDNode(RDNodeType::DYN_ALLOC);
    addNode(Inst, node);
    rb->append(node);

    const CallInst *CInst = cast<CallInst>(Inst);
    const Value *op;
    uint64_t size = 0, size2 = 0;

    switch (type) {
        case AllocationFunction::MALLOC:
        case AllocationFunction::ALLOCA:
            op = CInst->getOperand(0);
            break;
        case AllocationFunction::CALLOC:
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
    if (size != 0 && type == AllocationFunction::CALLOC) {
        // if this is call to calloc, the size is given
        // in the first argument too
        size2 = getConstantValue(CInst->getOperand(0));
        if (size2 != 0)
            size *= size2;
    }

    node->setSize(size);
    return node;
}

RDNode *LLVMRDBuilderSemisparse::createRealloc(const llvm::Instruction *Inst, RDBlock *rb)
{
    RDNode *node = new RDNode(RDNodeType::DYN_ALLOC);
    addNode(Inst, node);
    rb->append(node);

    uint64_t size = getConstantValue(Inst->getOperand(1));
    if (size == 0)
        size = Offset::UNKNOWN;
    else
        node->setSize(size);

    // realloc defines itself, since it copies the values
    // from previous memory
    node->addDef(node, 0, size, false /* strong update */);
    // operand 0 is pointer
    node->addUses(getPointsTo(Inst->getOperand(0),rb));

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

RDNode *LLVMRDBuilderSemisparse::createReturn(const llvm::Instruction *Inst, RDBlock *rb)
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
        node->addOverwrites(ptrNode, 0, Offset::UNKNOWN);
    }

    return node;
}

RDNode *LLVMRDBuilderSemisparse::getOperand(const llvm::Value *val, RDBlock *rb)
{
    RDNode *op = getNode(val);
    if (!op)
        return createNode(*llvm::cast<llvm::Instruction>(val), rb);

    return op;
}

RDNode *LLVMRDBuilderSemisparse::createNode(const llvm::Instruction &Inst, RDBlock *rb)
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

bool LLVMRDBuilderSemisparse::isStrongUpdate(const llvm::Value *val, const DefSite& ds, RDBlock *rb) {
    auto psn = PTA->getLLVMPointsTo(val);
    for (const auto& ptr : psn) {
        // this may emerge with vararg function
        if (llvm::isa<llvm::Function>(ptr.value))
            continue;

        RDNode *ptrNode = getOperand(ptr.value, rb);
        if (ptrNode != ds.target)
            continue;

        // strong update is possible only with must aliases. Also we can not
        // be pointing to heap, because then we don't know which object it
        // is in run-time, like:
        //  void *foo(int a)
        //  {
        //      void *mem = malloc(...)
        //      mem->n = a;
        //  }
        //
        //  1. mem1 = foo(3);
        //  2. mem2 = foo(4);
        //  3. assert(mem1->n == 3);
        //
        //  If we would do strong update on line 2 (which we would, since
        //  there we have must alias for the malloc), we would loose the
        //  definitions for line 1 and we would get incorrect results
        pta::PSNodeAlloc *target = pta::PSNodeAlloc::get(PTA->getPointsTo(ptr.value));
        assert(target && "Target of pointer is not an allocation");
        bool strong_update = psn.isSingleton() == 1 && !target->isHeap();
        return strong_update;
    }
    if (!ds.target->isUnknown())
        // the definition wasn't found, this should never happen
        assert(false && "Pointer that was in points-to set could not be found again. Points-to set has probably changed.");
    return false;
}

std::vector<DefSite> LLVMRDBuilderSemisparse::getPointsTo(const llvm::Value *val, RDBlock *rb)
{
    std::vector<DefSite> result;

    pta::PSNode *psn = PTA->getPointsTo(val);
    if (!psn)
        // don't have points-to information for used pointer
        return result;

    if (psn->pointsTo.empty()) {
#ifdef DEBUG_ENABLED
        llvm::errs() << "[RD] error: empty STORE points-to: " << *val << "\n";
#else
        // this may happen on invalid reads and writes to memory,
        // like when you try for example this:
        //
        //   int p, q;
        //   memcpy(p, q, sizeof p);
        //
        // (there should be &p and &q)
        // NOTE: maybe this is a bit strong to say unknown memory,
        // but better be sound then incorrect
        result.push_back(DefSite(UNKNOWN_MEMORY));
#endif
        return result;
    }

    for (const pta::Pointer& ptr: psn->pointsTo) {
        // XXX we should at least warn?
        if (ptr.isNull())
            continue;

        if (ptr.isUnknown()) {
            result.push_back(DefSite(UNKNOWN_MEMORY));
            continue;
        }

        // XXX: we should do something, shouldn't we?
        // Or we just slice only well-defined programs?
        if (ptr.isInvalidated())
            continue;

        const llvm::Value *ptrVal = ptr.target->getUserData<llvm::Value>();
        if (llvm::isa<llvm::Function>(ptrVal))
            continue;

        RDNode *ptrNode = getOperand(ptrVal, rb);
        if (!ptrNode) {
            // keeping such set is faster then printing it all to terminal
            // ... and we don't flood the terminal that way
            static std::set<const llvm::Value *> warned;
            if (warned.insert(ptrVal).second) {
                llvm::errs() << *ptrVal << "\n";
                llvm::errs() << "Don't have created node for pointer's target\n";
            }
            continue;
        }

        uint64_t size;
        if (ptr.offset.isUnknown()) {
            size = Offset::UNKNOWN;
        } else {
            size = ptr.target->getSize();
            if (size == 0)
                size = Offset::UNKNOWN;
        }

        // llvm::errs() << *Inst << " DEFS >> " << ptr.target->getName() << " ["
        //             << *ptr.offset << " - " << *ptr.offset + size - 1 << "\n";

        // strong update is possible only with must aliases. Also we can not
        // be pointing to heap, because then we don't know which object it
        // is in run-time, like:
        //  void *foo(int a)
        //  {
        //      void *mem = malloc(...)
        //      mem->n = a;
        //  }
        //
        //  1. mem1 = foo(3);
        //  2. mem2 = foo(4);
        //  3. assert(mem1->n == 3);
        //
        //  If we would do strong update on line 2 (which we would, since
        //  there we have must alias for the malloc), we would loose the
        //  definitions for line 1 and we would get incorrect results
        pta::PSNodeAlloc *target = pta::PSNodeAlloc::get(ptr.target);
        assert(target && "Target of pointer is not an allocation");
        /* bool strong_update = pts->pointsTo.size() == 1 && !target->isHeap(); */
        result.push_back(DefSite(ptrNode, ptr.offset, size));
    }
    return result;
}

RDNode *LLVMRDBuilderSemisparse::createLoad(const llvm::Instruction *Inst, RDBlock *rb)
{
    const llvm::LoadInst *LI = static_cast<const llvm::LoadInst *>(Inst);
    RDNode *node = new RDNode(RDNodeType::LOAD);
    addNode(Inst, node);
    rb->append(node);

    std::vector<DefSite> uses = getPointsTo(LI->getPointerOperand(), rb);
    for (auto& ds : uses) {
        ds.len = getAllocatedSize(LI->getPointerOperand()->getType()->getPointerElementType(), DL);
        if (ds.offset.isUnknown() || ds.len.offset == 0) {
            ds.len = Offset::UNKNOWN;
            ds.offset = 0;
        }
    }
    node->addUses(std::move(uses));

    return node;
}

RDNode *LLVMRDBuilderSemisparse::createStore(const llvm::Instruction *Inst, RDBlock *rb)
{
    RDNode *node = new RDNode(RDNodeType::STORE);
    addNode(Inst, node);
    rb->append(node);

    auto pts = getPointsTo(Inst->getOperand(1), rb);
    for (auto& ds : pts) {
        bool strong = false;
        if (!ds.offset.isUnknown() && ds.len.offset != 0) {
            ds.len = getAllocatedSize(Inst->getOperand(0)->getType(), DL);
            strong = isStrongUpdate(Inst->getOperand(1), ds, rb);
        } else {
            ds.offset = 0;
            ds.len = Offset::UNKNOWN;
        }
        node->addDef(ds, strong);
    }

    assert(node);
    return node;
}

template <typename OptsT>
static bool isRelevantCall(const llvm::Instruction *Inst,
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
        // function pointer call - we need that
        return true;

    if (func->size() == 0) {
        if (opts.getAllocationFunction(func->getName())
            != AllocationFunction::NONE)
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

static inline void makeEdge(RDNode *src, RDNode *dst)
{
    assert(src != dst && "Tried creating self-loop");
    assert(src != nullptr);
    // This is checked by addSuccessor():
    // assert(dst != nullptr);

    src->addSuccessor(dst);
}

/**
 * returns all RDBlock-s to which the LLVM block maps.
 */
std::vector<RDBlock *>
LLVMRDBuilderSemisparse::buildBlock(const llvm::BasicBlock& block)
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
                    if (!isRelevantCall(&Inst, _options))
                        break;

                    std::pair<RDNode *, RDNode *> subg = createCall(&Inst, rb);

                    makeEdge(last_node, subg.first);

                    if (subg.first->successorsNum() > 0) {
                        RDBlock *succBB = (*subg.first->getSuccessors().begin())->getBBlock();
                        if (succBB != nullptr) {
                            rb->addSuccessor(succBB);
                        }
                    }

                    // if the function consists of single node(intrinsics, undefined etc)
                    // do not add more nodes for this function
                    if (subg.first != subg.second) {
                        // successors for blocks with return will be added later
                        new_block = new RDBlock();
                        addBlock(const_cast<llvm::BasicBlock *>(&block), new_block);
                        result.push_back(new_block);
                        new_block->append(subg.second);
                        rb = new_block;

                        for (RDNode *pred : subg.second->getPredecessors()) {
                            RDBlock *predBB = pred->getBBlock();
                            if (predBB) {
                                for (RDNode *succ : pred->getSuccessors()) {
                                    if (succ == subg.second)
                                        continue;
                                    RDBlock *succBB = succ->getBBlock();
                                    if (succBB)
                                        predBB->addSuccessor(succBB);
                                }
                            }
                        }

                        for (RDNode *pred : subg.second->getPredecessors()) {
                            RDBlock *predBB = pred->getBBlock();
                            if (pred != subg.second && predBB) {
                                predBB->addSuccessor(rb);
                            } else if (!predBB) {
                                for (RDNode *p2 : pred->getPredecessors()) {
                                    if (p2->getBBlock() != predBB) {
                                        p2->getBBlock()->addSuccessor(rb);
                                        p2->getBBlock()->append(pred);
                                    }
                                }
                            }
                        }
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
        if (node && last_node != node) {
            makeEdge(last_node, node);
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
            makeEdge(ptan->getLastNode(), succ->getFirstNode());
            ptan->addSuccessor(succ);
            ++num;
        }
    }

    return num;
}

std::pair<RDNode *, RDNode *>
LLVMRDBuilderSemisparse::createCallToFunction(const llvm::Function *F, RDBlock *rb)
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
    makeEdge(callNode, root);
    makeEdge(ret, returnNode);
    if (root->getBBlock())
        rb->addSuccessor(root->getBBlock());

    return std::make_pair(callNode, returnNode);
}

std::pair<RDBlock *, RDBlock *>
LLVMRDBuilderSemisparse::buildFunction(const llvm::Function& F)
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

    for (const llvm::BasicBlock& block : F) {
        std::vector<RDBlock *> blocks = buildBlock(block);
        if (!first) {
            first = blocks[0]->getFirstNode();
            fstblock = blocks[0];
            fstblock->prepend(root);
        }

        built_blocks[&block] = std::move(blocks);
    }

    assert(first && fstblock);
    makeEdge(root, first);
    fstblock->prepend(root);

    std::vector<RDNode *> rets;
    const llvm::BasicBlock *last_llvm_block = nullptr;

    RDBlock *artificial_ret = new RDBlock();
    artificial_ret->append(ret);

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
        }
        RDBlock *last_subblock = it->second.back();
        // assert((ptan.first && ptan.second) || (!ptan.first && !ptan.second));
        if (!last_subblock->getFirstNode())
            continue;
        // add successors to this block (skipping the empty blocks)
        // FIXME: this function is shared with PSS, factor it out
        size_t succ_num = blockAddSuccessors(built_blocks, last_subblock, block);
        // if we have not added any successor, then the last node
        // of this block is a return node
        if (succ_num == 0 && last_subblock->getLastNode()->getType() == RDNodeType::RETURN)
            rets.push_back(last_subblock->getLastNode());
        last_llvm_block = &block;
    }
    // push the artificial return block
    // artificial_ret needs to be the last block
    built_blocks[last_llvm_block].push_back(artificial_ret);

    // add successors edges from every real return to our artificial ret node
    for (RDNode *r : rets) {
        makeEdge(r, ret);
        assert( r->getBBlock() );
        r->getBBlock()->addSuccessor(artificial_ret);
    }

    functions_blocks[&F] = std::move(built_blocks);
    addBlock(last_llvm_block, artificial_ret);
    return std::make_pair(fstblock, artificial_ret);
}

RDNode *LLVMRDBuilderSemisparse::createUndefinedCall(const llvm::CallInst *CInst, RDBlock *rb)
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

            if (ptr.isInvalidated())
                continue;

            const llvm::Value *ptrVal = ptr.target->getUserData<llvm::Value>();
            if (llvm::isa<llvm::Function>(ptrVal))
                // function may not be redefined
                continue;

            RDNode *target = getOperand(ptrVal, rb);
            assert(target && "Don't have pointer target for call argument");

            // this call may define or use this memory
            if (!_options.undefinedArePure)
                node->addDef(target, Offset::UNKNOWN, Offset::UNKNOWN);
            node->addUse(DefSite(target, Offset::UNKNOWN, Offset::UNKNOWN));
        }
    }

    // XXX: to be completely correct, we should assume also modification
    // of all global variables, so we should perform a write to
    // unknown memory instead of the loop above

    return node;
}

RDNode *LLVMRDBuilderSemisparse::createIntrinsicCall(const llvm::CallInst *CInst, RDBlock *rb)
{
    using namespace llvm;

    const IntrinsicInst *I = cast<IntrinsicInst>(CInst);
    const Value *dest = nullptr;
    const Value *lenVal = nullptr;
    const Value *source = nullptr;

    RDNode *ret;
    pta::PSNode *pts2;

    switch (I->getIntrinsicID())
    {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
            source = I->getOperand(1);
            // fall-through
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
            ret->addDef(ret, 0, Offset::UNKNOWN);
            pts2 = PTA->getPointsTo(I->getOperand(0));
            assert(pts2 && "No points-to information");
            for (const pta::Pointer& ptr : pts2->pointsTo) {
                if (!ptr.isValid())
                    continue;

                const llvm::Value *ptrVal = ptr.target->getUserData<llvm::Value>();
                if (llvm::isa<llvm::Function>(ptrVal))
                    continue;

                uint64_t from, to;
                uint64_t len = Offset::UNKNOWN;
                if (ptr.offset.isUnknown()) {
                    // if the offset is UNKNOWN, use whole memory
                    from = Offset::UNKNOWN;
                    len = Offset::UNKNOWN;
                } else {
                    from = *ptr.offset;
                }

                // do not allow overflow
                if (Offset::UNKNOWN - from > len)
                    to = from + len;
                else
                    to = Offset::UNKNOWN;

                RDNode *target = getOperand(ptrVal, rb);
                assert(target && "Don't have pointer target for intrinsic call");

                // add the definition
                ret->addUse(target, from, to);
            }
            addNode(CInst, ret);
            rb->append(ret);
            return ret;
        default:
            return createUndefinedCall(CInst, rb);
    }

    ret = new RDNode(RDNodeType::CALL);
    rb->append(ret);
    addNode(CInst, ret);

    pta::PSNode *pts = PTA->getPointsTo(dest);
    assert(pts && "No points-to information");

    uint64_t len = Offset::UNKNOWN;
    if (const ConstantInt *C = dyn_cast<ConstantInt>(lenVal))
        len = C->getLimitedValue();

    for (const pta::Pointer& ptr : pts->pointsTo) {
        if (!ptr.isValid() || ptr.isInvalidated())
            continue;

        const llvm::Value *ptrVal = ptr.target->getUserData<llvm::Value>();
        if (llvm::isa<llvm::Function>(ptrVal))
            continue;

        uint64_t from, to;
        if (ptr.offset.isUnknown()) {
            // if the offset is UNKNOWN, use whole memory
            from = Offset::UNKNOWN;
            len = Offset::UNKNOWN;
        } else {
            from = *ptr.offset;
        }

        // do not allow overflow
        if (Offset::UNKNOWN - from > len)
            to = from + len;
        else
            to = Offset::UNKNOWN;

        RDNode *target = getOperand(ptrVal, rb);
        assert(target && "Don't have pointer target for intrinsic call");

        // add the definition
        ret->addDef(target, from, to, true /* strong update */);
    }

    if (source) {
        pts2 = PTA->getPointsTo(source);
        assert(pts && "No points-to information");
        for (const pta::Pointer& ptr : pts2->pointsTo) {
            if (!ptr.isValid())
                continue;

            const llvm::Value *ptrVal = ptr.target->getUserData<llvm::Value>();
            if (llvm::isa<llvm::Function>(ptrVal))
                continue;

            uint64_t from, to;
            if (ptr.offset.isUnknown()) {
                // if the offset is UNKNOWN, use whole memory
                from = Offset::UNKNOWN;
                len = Offset::UNKNOWN;
            } else {
                from = *ptr.offset;
            }

            // do not allow overflow
            if (Offset::UNKNOWN - from > len)
                to = from + len;
            else
                to = Offset::UNKNOWN;

            RDNode *target = getOperand(ptrVal, rb);
            assert(target && "Don't have pointer target for intrinsic call");

            // add the definition
            ret->addUse(target, from, to);
        }
    }

    return ret;
}

std::pair<RDNode *, RDNode *>
LLVMRDBuilderSemisparse::createCall(const llvm::Instruction *Inst, RDBlock *rb)
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
                auto type = _options.getAllocationFunction(func->getName());
                if (type != AllocationFunction::NONE) {
                    if (type == AllocationFunction::REALLOC)
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
            llvm::errs() << "[RD] error: a call via a function pointer, "
                            "but the points-to is empty\n"
                         << *CInst << "\n";
            RDNode *n = createUndefinedCall(CInst, rb);
            return std::make_pair(n, n);
        }

        RDNode *call_funcptr = nullptr, *ret_call = nullptr;

        if (op->pointsTo.size() > 1) {
            for (const pta::Pointer& ptr : op->pointsTo) {
                if (!ptr.isValid() || ptr.isInvalidated())
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

                makeEdge(call_funcptr, cf.first);
                rb->addSuccessor((*cf.first->getSuccessors().begin())->getBBlock());
                makeEdge(cf.second, ret_call);
            }
        } else {
            // don't add redundant nodes if not needed
            const pta::Pointer& ptr = *(op->pointsTo.begin());
            if (ptr.isValid()) {
                const llvm::Value *valF = ptr.target->getUserData<llvm::Value>();
                if (const llvm::Function *F = llvm::dyn_cast<llvm::Function>(valF)) {
                    if (F->size() == 0) {
                        RDNode *n = createUndefinedCall(CInst, rb);
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
            return std::make_pair(n, n);
        }

        assert(call_funcptr && ret_call);
        return std::make_pair(call_funcptr, ret_call);
    }
}

ReachingDefinitionsGraph LLVMRDBuilderSemisparse::build()
{
    // get entry function
    llvm::Function *F = M->getFunction(_options.entryFunction);
    if (!F) {
        llvm::errs() << "The function '" << _options.entryFunction << "' was not found in the module\n";
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
        makeEdge(glob->getLastNode(), root);

        assert(root->successorsNum() > 0);
        root = glob->getFirstNode();
    } else {
        delete glob;
    }

    ReachingDefinitionsGraph graph;
    graph.setRoot(root);

    return graph;
}

static uint64_t getGlobalVariableSize(const llvm::GlobalVariable *var, const llvm::DataLayout *DL)
{
    if (var->getType()->isArrayTy()) {
        return var->getType()->getArrayNumElements() * getAllocatedSize(var->getType()->getArrayElementType(), DL);
    } else {
        return getAllocatedSize(var->getType(), DL);
    }
}

RDBlock *LLVMRDBuilderSemisparse::buildGlobals()
{
    RDBlock *glob = new RDBlock();
    RDNode *cur = nullptr, *prev, *first = nullptr;
    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        prev = cur;

        // every global node is like memory allocation
        cur = new RDNode(RDNodeType::ALLOC);
        cur->setSize(getGlobalVariableSize(&*I, DL));
        // some global variables are initialized on creation
        if (I->hasInitializer())
            cur->addDef(cur, 0, cur->getSize(), true);
        addNode(&*I, cur);
        glob->append(cur);

        if (prev)
            makeEdge(prev, cur);
        else
            first = cur;
    }

    assert((!first && !cur) || (first && cur));
    return glob;
}

} // namespace rd
} // namespace analysis
} // namespace dg
