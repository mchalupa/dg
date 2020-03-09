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

#include <llvm/IR/Dominators.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/PointerAnalysis/PointerGraph.h"
#include "dg/ADT/Queue.h"

#include "llvm/ForkJoin/ForkJoin.h"
#include "llvm/ReadWriteGraph/LLVMReadWriteGraphBuilder.h"
#include "llvm/llvm-utils.h"

namespace dg {
namespace dda {

struct ValInfo {
    const llvm::Value *v;
    ValInfo(const llvm::Value *val) : v(val) {}
};

llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const ValInfo& vi) {
    using namespace llvm;

    if (auto I = dyn_cast<Instruction>(vi.v)) {
        os << I->getParent()->getParent()->getName()
               << ":: " << *I;
    } else if (auto A = dyn_cast<Argument>(vi.v)) {
        os << A->getParent()->getParent()->getName()
               << ":: (arg) " << *A;
    } else if (auto F = dyn_cast<Function>(vi.v)) {
        os << "(func) " << F->getName();
    } else {
        os << *vi.v;
    }

    return os;
}

static inline void makeEdge(RWNode *src, RWNode *dst)
{
    assert(src != nullptr);
    src->addSuccessor(dst);
}

RWNode *LLVMReadWriteGraphBuilder::createAlloc(const llvm::Instruction *Inst)
{
    RWNode *node = create(RWNodeType::ALLOC);
    auto iterator = nodes_map.find(Inst);
    if (iterator == nodes_map.end()) {
        addNode(Inst, node);
    } else {
        assert(iterator->second->getType() == RWNodeType::CALL &&
               "Adding node we already have");
        addArtificialNode(Inst, node);
        makeEdge(iterator->second, node);
    }

    if (const llvm::AllocaInst *AI
            = llvm::dyn_cast<llvm::AllocaInst>(Inst))
        node->setSize(llvmutils::getAllocatedSize(AI, &M->getDataLayout()));

    return node;
}

RWNode *LLVMReadWriteGraphBuilder::createDynAlloc(const llvm::Instruction *Inst,
                                                  AllocationFunction type)
{
    using namespace llvm;

    RWNode *node = create(RWNodeType::DYN_ALLOC);
    auto iterator = nodes_map.find(Inst);
    if (iterator == nodes_map.end()) {
        addNode(Inst, node);
    } else {
        assert(iterator->second->getType() == RWNodeType::CALL &&
               "Adding node we already have");
        addArtificialNode(Inst, node);
    }

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
    size = llvmutils::getConstantValue(op);
    if (size != 0 && type == AllocationFunction::CALLOC) {
        // if this is call to calloc, the size is given
        // in the first argument too
        size2 = llvmutils::getConstantValue(CInst->getOperand(0));
        if (size2 != 0)
            size *= size2;
    }

    node->setSize(size);
    return node;
}

RWNode *LLVMReadWriteGraphBuilder::createRealloc(const llvm::Instruction *Inst)
{
    RWNode *node = create(RWNodeType::DYN_ALLOC);
    auto iterator = nodes_map.find(Inst);
    if (iterator == nodes_map.end()) {
        addNode(Inst, node);
    } else {
        assert(iterator->second->getType() == RWNodeType::CALL &&
               "Adding node we already have");
        addArtificialNode(Inst, node);
    }

    uint64_t size = llvmutils::getConstantValue(Inst->getOperand(1));
    if (size == 0)
        size = Offset::UNKNOWN;
    else
        node->setSize(size);

    // realloc defines itself, since it copies the values
    // from previous memory
    node->addDef(node, 0, size, false /* strong update */);

    if (buildUses) {
        // realloc copies the memory
        auto defSites = mapPointers(Inst, Inst->getOperand(0), size);
        for (const auto& ds : defSites) {
            node->addUse(ds);
        }
    }

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

RWNode *LLVMReadWriteGraphBuilder::createReturn(const llvm::Instruction *Inst)
{
    RWNode *node = create(RWNodeType::RETURN);
    addNode(Inst, node);

    if (!forgetLocalsAtReturn)
        return node;

    // FIXME: don't do that for every return instruction,
    // compute it only once for a function
    std::set<const llvm::Value *> locals;
    getLocalVariables(Inst->getParent()->getParent(),
                      locals);

    for (const llvm::Value *ptrVal : locals) {
        RWNode *ptrNode = getOperand(ptrVal);
        if (!ptrNode) {
            llvm::errs() << ValInfo(ptrVal) << "\n";
            llvm::errs() << "Don't have created node for local variable\n";
            abort();
        }

        // make this return node behave like we overwrite the definitions.
        // We actually don't override them, therefore they are dropped
        // and that is what we want (we don't want to propagade
        // local definitions from functions into callees)
        // TODO: this is a feature of the data-flow analysis,
        // it is not working with the SSA analysis. We should really
        // have some strict line between them.
        node->addOverwrites(ptrNode, 0, Offset::UNKNOWN);
    }

    return node;
}

RWNode *LLVMReadWriteGraphBuilder::getOperand(const llvm::Value *val)
{
    auto op = getNode(val);
    if (!op) {
        // Allocations are targets of pointers. When we are creating
        // stores/loads, we iterate over points-to set and get the allocations.
        // But we may have not the function with allocation created yet,
        // so create it now if such a case occurs.
        if (auto I = llvm::dyn_cast<llvm::AllocaInst>(val)) {
            op = createAlloc(I);
        } else if (auto CI = llvm::dyn_cast<llvm::CallInst>(val)) {
            const auto calledVal = CI->getCalledValue()->stripPointerCasts();
            const auto func = llvm::dyn_cast<llvm::Function>(calledVal);
            if (_options.isAllocationFunction(func->getName())) {
                auto call = createCall(CI);
                assert(call.first == call.second);
                assert(call.first->getType() == RWNodeType::DYN_ALLOC);
                op = call.first;
            }
        }
        if (!op) {
            llvm::errs() << "[RD] error: cannot find an operand: " << *val << "\n";
        }
    }
    assert(op && "Do not have an operand");
    return op;
}

RWNode *LLVMReadWriteGraphBuilder::createStore(const llvm::Instruction *Inst)
{
    RWNode *node = create(RWNodeType::STORE);
    addNode(Inst, node);

    uint64_t size = llvmutils::getAllocatedSize(Inst->getOperand(0)->getType(),
                                                &M->getDataLayout());
    if (size == 0)
        size = Offset::UNKNOWN;

    auto defSites = mapPointers(Inst, Inst->getOperand(1), size);

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
    bool strong_update = false;
    if (defSites.size() == 1) {
        const auto& ds = *(defSites.begin());
        strong_update = !ds.offset.isUnknown() && !ds.len.isUnknown() &&
                         ds.target->getType() != RWNodeType::DYN_ALLOC;
    }

    for (const auto& ds : defSites) {
        node->addDef(ds, strong_update);
    }

    assert(node);
    return node;
}

RWNode *LLVMReadWriteGraphBuilder::createLoad(const llvm::Instruction *Inst)
{
    RWNode *node = create(RWNodeType::LOAD);
    addNode(Inst, node);

    uint64_t size = llvmutils::getAllocatedSize(Inst->getType(),
                                                &M->getDataLayout());
    if (size == 0)
        size = Offset::UNKNOWN;

    auto defSites = mapPointers(Inst, Inst->getOperand(0), size);
    for (const auto& ds : defSites) {
        node->addUse(ds);
    }

    assert(node);
    return node;
}

template <typename OptsT>
static bool isRelevantCall(const llvm::Instruction *Inst,
                           OptsT& opts)
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
        // we have a model for this function
        if (opts.getFunctionModel(func->getName()))
            return true;
        // memory allocation
        if (opts.isAllocationFunction(func->getName()))
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

LLVMReadWriteGraphBuilder::Block&
LLVMReadWriteGraphBuilder::buildBlockNodes(Subgraph& subg,
                                           const llvm::BasicBlock& llvmBlock) {
    using namespace llvm;

    Block& block = subg.createBlock(&llvmBlock);

    for (const Instruction& Inst : llvmBlock) {
        // we may created this node when searching for an operand
        auto node = getNode(&Inst);
        if (!node) {
           switch(Inst.getOpcode()) {
                case Instruction::Alloca:
                    // we need alloca's as target to DefSites
                    node = createAlloc(&Inst);
                    break;
                case Instruction::Store:
                    node = createStore(&Inst);
                    break;
                case Instruction::Load:
                    if (buildUses)
                        node = createLoad(&Inst);
                    break;
                case Instruction::Ret:
                    // we need create returns, since
                    // these modify CFG and thus data-flow
                    // FIXME: add new type of node NOOP,
                    // and optimize it away later
                    node = createReturn(&Inst);
                    break;
                case Instruction::Call:
                    if (!isRelevantCall(&Inst, _options))
                        break;

                    auto call = createCall(&Inst);
                    assert(call.first != nullptr);

                    // the call does not return, bail out
                    if (!call.second)
                        return block;

                    if (call.first != call.second) {
                        // this call does return something
                        block.nodes.push_back(call.first);
                        block.nodes.push_back(call.second);
                        node = nullptr;
                    } else {
                        node = call.first;
                    }
                    break;
            }
        }

        if (node) {
            block.nodes.push_back(node);
        }
    }

    return block;
}

// return first and last nodes of the block
LLVMReadWriteGraphBuilder::Block&
LLVMReadWriteGraphBuilder::buildBlock(Subgraph& subg,
                                      const llvm::BasicBlock& llvmBlock)
{
    auto& block = buildBlockNodes(subg, llvmBlock);

    // add successors between nodes except for call instructions
    // that will have as successors the entry nodes of subgraphs
    RWNode *last = nullptr;
    for (auto nd : block.nodes) {
        assert(nd == block.nodes.back() || nd->getType() != RWNodeType::RETURN);

        if (last) {
            if (!(last->getType() == RWNodeType::CALL
                   && nd->getType() == RWNodeType::CALL_RETURN)) {
                last->addSuccessor(nd);
            }
        }

        last = nd;
    }

    return block;
}

void LLVMReadWriteGraphBuilder::blockAddSuccessors(
                                       LLVMReadWriteGraphBuilder::Subgraph& subg,
                                       LLVMReadWriteGraphBuilder::Block& block,
                                       const llvm::BasicBlock *llvmBlock,
                                       std::set<const llvm::BasicBlock *>& visited)
{
    assert(!block.nodes.empty() && "Block is empty");

    for (auto S = llvm::succ_begin(llvmBlock),
              SE = llvm::succ_end(llvmBlock); S != SE; ++S) {

        // we already processed this block? Then don't try to add the edges again
        // FIXME: get rid of this... we can check whether we saw the RWBBlock...
        if (!visited.insert(*S).second)
           continue;

        auto succIt = subg.blocks.find(*S);
        if ((succIt == subg.blocks.end() ||
            succIt->second.nodes.empty())) {
            // if we don't have this block built (there was no
            // relevant instruction), we must pretend to be there for
            // control flow information. Thus instead of adding it as
            // the successor, add its successors as successors
            blockAddSuccessors(subg, block, *S, visited);
        } else {
            // add an edge to the first node of the successor block
            assert(!succIt->second.nodes.empty());
            makeEdge(block.nodes.back(), succIt->second.nodes.front());
        }
    }
}

LLVMReadWriteGraphBuilder::Subgraph *
LLVMReadWriteGraphBuilder::getOrCreateSubgraph(const llvm::Function *F) {
    // reuse built subgraphs if available, so that we won't get
    // stuck in infinite loop with recursive functions
    Subgraph *subg = nullptr;
    auto it = subgraphs_map.find(F);
    if (it == subgraphs_map.end()) {
        // create a new subgraph
        subg = &buildFunction(*F);
        assert(subg->entry && "No entry in the subgraph");
        assert(subg->entry->nodes.front() && "No first node in the subgraph");
    } else {
        subg = &it->second;
    }

    assert(subg && "No subgraph");
    return subg;
}

std::pair<RWNode *, RWNode *>
LLVMReadWriteGraphBuilder::createCallToFunction(const llvm::Function *F,
                                    const llvm::CallInst * CInst)
{
    assert(nodes_map.find(CInst) == nodes_map.end()
            && "Already created this function");

    if (auto model = _options.getFunctionModel(F->getName())) {
        auto node = funcFromModel(model, CInst);
        addNode(CInst, node);
        return {node, node};
    } else if (F->size() == 0) {
        auto node = createCallToZeroSizeFunction(F, CInst);
        return {node, node};
    } else if (!llvmutils::callIsCompatible(F, CInst)) {
        llvm::errs() << "[RD] error: call of incompatible function: "
                     << ValInfo(CInst) << "\n";
        llvm::errs() << "            Calling : "
                     << F->getName() << " of type " << *F->getType() << "\n";
        auto node = createUndefinedCall(CInst);
        return {node, node};
    }

    RWNode *callNode = create(RWNodeType::CALL);
    RWNode *returnNode = create(RWNodeType::CALL_RETURN);

    addNode(CInst, callNode);

    // just create the subgraph, we'll add the edges later
    // once we have created all the graphs -- this is due
    // to recursive procedures
    Subgraph *s = getOrCreateSubgraph(F);
    calls[{callNode, returnNode}].insert(s);

    return {callNode, returnNode};
}

std::pair<RWNode *, RWNode *>
LLVMReadWriteGraphBuilder::createCallToFunctions(const std::vector<const llvm::Function *> &functions,
                                     const llvm::CallInst *CInst) {

    assert(!functions.empty() && "No functions to call");
    assert(nodes_map.find(CInst) == nodes_map.end()
            && "Already created this function");

    RWNode *callNode = create(RWNodeType::CALL);
    RWNode *returnNode = create(RWNodeType::CALL_RETURN);

    auto& callsSet = calls[{callNode, returnNode}];

    std::set <const llvm::Function *> incompatibleCalls;
    for (auto F : functions) {
        if (!llvmutils::callIsCompatible(F, CInst)) {
            incompatibleCalls.insert(F);
            continue;
        }

        RWNode *onenode = nullptr;
        if (auto model = _options.getFunctionModel(F->getName())) {
            onenode = funcFromModel(model, CInst);
            addNode(CInst, onenode);
        } else if (F->size() == 0) {
            onenode = createCallToZeroSizeFunction(F, CInst);
        }

        if (onenode) {
            makeEdge(callNode, onenode);
            makeEdge(onenode, returnNode);

            continue;
        }

        // proper function... finally. Create the subgraph
        // if not created yet.
        Subgraph *s = getOrCreateSubgraph(F);
        callsSet.insert(s);
    }

    if (!incompatibleCalls.empty()) {
#ifndef NDEBUG
        llvm::errs() << "[RD] warning: incompatible function pointers for "
                     << ValInfo(CInst) << "\n";
        for (auto *F : incompatibleCalls) {
            llvm::errs() << "   Tried call: " << F->getName() << " of type "
                         << *F->getType() << "\n";
        }
        if (incompatibleCalls.size() == functions.size()) {
            llvm::errs() << "[RD] error: did not find any compatible pointer for this call.\n";
        }
#else
        if (incompatibleCalls.size() == functions.size()) {
            llvm::errs() << "[RD] error: did not find any compatible function pointer for "
                         << ValInfo(CInst) << "\n";
            for (auto *F : incompatibleCalls) {
                llvm::errs() << "   Tried call: " << F->getName() << " of type "
                             << *F->getType() << "\n";
            }
        }
#endif // not NDEBUG
    }

    return {callNode, returnNode};
}

// Get llvm BasicBlock's in levels of Dominator Tree (BFS order through the dominator tree)
// FIXME: Copied from PointerSubgraph.cpp
static std::vector<const llvm::BasicBlock *>
getBasicBlocksInDominatorOrder(llvm::Function& F)
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

LLVMReadWriteGraphBuilder::Subgraph&
LLVMReadWriteGraphBuilder::buildFunction(const llvm::Function& F)
{
    // emplace new subgraph to avoid looping with recursive functions
    auto si = subgraphs_map.emplace(&F, Subgraph());
    Subgraph& subg = si.first->second;
    subg.rwsubgraph = graph.createSubgraph();

    ///
    // Create blocks
    //

    // iterate over the blocks in dominator-tree order
    // so that all operands are created before their uses
    for (const auto *llvmBlock :
              getBasicBlocksInDominatorOrder(const_cast<llvm::Function&>(F))) {

        auto& block = buildBlock(subg, *llvmBlock);

        // save the entry block and ensure that it has
        // at least one node (so that we have something to start from)
        if (subg.entry == nullptr) {
            subg.entry = &block;
            if (block.nodes.empty()) {
                block.nodes.push_back(create(RWNodeType::PHI));
            }
        } else {
            // do not keep empty blocks
            if (block.nodes.empty()) {
                subg.blocks.erase(llvmBlock);
            }
        }
    }

    ///
    // Set successors of blocks
    //
    for (auto& it : subg.blocks) {
        auto llvmBlock = it.first;
        auto& block = it.second;

        // we remove the empty blocks
        assert(!block.nodes.empty());

        // add successors to this block (skipping the empty blocks)
        std::set<const llvm::BasicBlock*> visited;
        blockAddSuccessors(subg, block, llvmBlock, visited);

        // collect the return nodes (move it to append() method of block?)
        if (!block.nodes.empty() &&
            block.nodes.back()->getType() == RWNodeType::RETURN) {
            subg.returns.push_back(block.nodes.back());
        }
    }

    return subg;
}

RWNode *LLVMReadWriteGraphBuilder::createUndefinedCall(const llvm::CallInst *CInst)
{
    using namespace llvm;

    assert((nodes_map.find(CInst) == nodes_map.end())
           && "Adding node we already have");

    RWNode *node = create(RWNodeType::CALL);
    addNode(CInst, node);

    // if we assume that undefined functions are pure
    // (have no side effects), we can bail out here
    if (_options.undefinedArePure)
        return node;

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

        auto pts = PTA->getLLVMPointsToChecked(llvmOp);
        // if we do not have a pts, this is not pointer
        // relevant instruction. We must do it this way
        // instead of type checking, due to the inttoptr.
        if (!pts.first)
            continue;

        for (const auto& ptr : pts.second) {
            if (llvm::isa<llvm::Function>(ptr.value))
                // function may not be redefined
                continue;

            RWNode *target = getOperand(ptr.value);
            assert(target && "Don't have pointer target for call argument");

            // this call may use and define this memory
            node->addDef(target, Offset::UNKNOWN, Offset::UNKNOWN);
            node->addUse(target, Offset::UNKNOWN, Offset::UNKNOWN);
        }
    }

    // XXX: to be completely correct, we should assume also modification
    // of all global variables, so we should perform a write to
    // unknown memory instead of the loop above

    return node;
}

bool LLVMReadWriteGraphBuilder::isInlineAsm(const llvm::Instruction *instruction)
{
    const llvm::CallInst *callInstruction = llvm::cast<llvm::CallInst>(instruction);
    return callInstruction->isInlineAsm();
}

void LLVMReadWriteGraphBuilder::matchForksAndJoins()
{
    using namespace llvm;
    using namespace pta;

    ForkJoinAnalysis FJA{PTA};

    for (auto& it : threadJoinCalls) {
        // it.first -> llvm::CallInst, it.second -> RWNode *
        auto functions = FJA.joinFunctions(it.first);
        for (auto llvmFunction : functions) {
            auto graphIterator = subgraphs_map.find(llvmFunction);
            for (auto returnNode : graphIterator->second.returns) {
                makeEdge(returnNode, it.second);
            }
        }
    }
}

RWNode *LLVMReadWriteGraphBuilder::createIntrinsicCall(const llvm::CallInst *CInst)
{
    using namespace llvm;

    const IntrinsicInst *I = cast<IntrinsicInst>(CInst);
    const Value *dest;
    const Value *lenVal;

    RWNode *ret;
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
            ret = create(RWNodeType::CALL);
            ret->addDef(ret, 0, Offset::UNKNOWN);
            addNode(CInst, ret);
            return ret;
        default:
            return createUndefinedCall(CInst);
    }

    ret = create(RWNodeType::CALL);
    addNode(CInst, ret);

    auto pts = PTA->getLLVMPointsToChecked(dest);
    if (!pts.first) {
        llvm::errs() << "[RD] Error: No points-to information for destination in\n";
        llvm::errs() << ValInfo(I) << "\n";
        // continue, the points-to set is {unknown}
    }

    uint64_t len = Offset::UNKNOWN;
    if (const ConstantInt *C = dyn_cast<ConstantInt>(lenVal))
        len = C->getLimitedValue();

    for (const auto& ptr : pts.second) {
        if (llvm::isa<llvm::Function>(ptr.value))
            continue;

        Offset from, to;
        if (ptr.offset.isUnknown()) {
            // if the offset is UNKNOWN, use whole memory
            from = Offset::UNKNOWN;
            len = Offset::UNKNOWN;
        } else {
            from = *ptr.offset;
        }

        // do not allow overflow
        if (Offset::UNKNOWN - *from > len)
            to = from + len;
        else
            to = Offset::UNKNOWN;

        RWNode *target = getOperand(ptr.value);
        //assert(target && "Don't have pointer target for intrinsic call");
        if (!target) {
            // keeping such set is faster then printing it all to terminal
            // ... and we don't flood the terminal that way
            static std::set<const llvm::Value *> warned;
            if (warned.insert(ptr.value).second) {
                llvm::errs() << "[RD] error at " << ValInfo(CInst) << "\n"
                             << "[RD] error: Haven't created node for: "
                             << ValInfo(ptr.value) << "\n";
            }
            target = UNKNOWN_MEMORY;
        }

        // add the definition
        ret->addDef(target, from, to, !from.isUnknown() && !to.isUnknown() /* strong update */);
    }

    return ret;
}

template <typename T>
std::pair<Offset, Offset> getFromTo(const llvm::CallInst *CInst, T what) {
    auto from = what->from.isOperand()
                ? llvmutils::getConstantValue(CInst->getArgOperand(what->from.getOperand()))
                  : what->from.getOffset();
    auto to = what->to.isOperand()
                ? llvmutils::getConstantValue(CInst->getArgOperand(what->to.getOperand()))
                  : what->to.getOffset();

    return {from, to};
}

RWNode *LLVMReadWriteGraphBuilder::funcFromModel(const FunctionModel *model, const llvm::CallInst *CInst) {

    RWNode *node = create(RWNodeType::CALL);

    for (unsigned int i = 0; i < CInst->getNumArgOperands(); ++i) {
        if (!model->handles(i))
            continue;

        const auto llvmOp = CInst->getArgOperand(i);
        auto pts = PTA->getLLVMPointsToChecked(llvmOp);
        // if we do not have a pts, this is not pointer
        // relevant instruction. We must do it this way
        // instead of type checking, due to the inttoptr.
        if (!pts.first) {
            llvm::errs() << "[Warning]: did not find pt-set for modeled function\n";
            llvm::errs() << "           Func: " << model->name << ", operand " << i << "\n";
            continue;
        }

        for (const auto& ptr : pts.second) {
            if (llvm::isa<llvm::Function>(ptr.value))
                // functions may not be redefined
                continue;

            RWNode *target = getOperand(ptr.value);
            assert(target && "Don't have pointer target for call argument");

            Offset from, to;
            if (auto defines = model->defines(i)) {
                std::tie(from, to) = getFromTo(CInst, defines);
                // this call may define this memory
                bool strong_updt = pts.second.size() == 1 &&
                                   !ptr.offset.isUnknown() &&
                                   !(ptr.offset + from).isUnknown() &&
                                   !(ptr.offset + to).isUnknown() &&
                                   !llvm::isa<llvm::CallInst>(ptr.value);
                // FIXME: what about vars in recursive functions?
                node->addDef(target, ptr.offset + from, ptr.offset + to, strong_updt);
            }
            if (auto uses = model->uses(i)) {
                std::tie(from, to) = getFromTo(CInst, uses);
                // this call uses this memory
                node->addUse(target, ptr.offset + from, ptr.offset + to);
            }
        }
    }

    return node;
}

std::pair<RWNode *, RWNode *>
LLVMReadWriteGraphBuilder::createCall(const llvm::Instruction *Inst)
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
        RWNode *node = createUndefinedCall(CInst);
        return {node, node};
    }

    if (const Function *function = dyn_cast<Function>(calledVal)) {
        return createCallToFunction(function, CInst);
    }


    const auto& functions = getCalledFunctions(calledVal, PTA);
    if (functions.empty()) {
        llvm::errs() << "[RD] error: could not determine the called function "
                        "in a call via pointer: \n"
                     << ValInfo(CInst) << "\n";
        RWNode *node = createUndefinedCall(CInst);
        return {node, node};
    }
    return createCallToFunctions(functions, CInst);
}

RWNode *
LLVMReadWriteGraphBuilder::createCallToZeroSizeFunction(const llvm::Function *function,
                                            const llvm::CallInst *CInst)
{
    if (function->isIntrinsic()) {
        return createIntrinsicCall(CInst);
    }
    if (_options.threads) {
        if (function->getName() == "pthread_create") {
            return createPthreadCreateCalls(CInst);
        } else if (function->getName() == "pthread_join") {
            return createPthreadJoinCall(CInst);
        } else if (function->getName() == "pthread_exit") {
            return createPthreadExitCall(CInst);
        }
    }

    auto type = _options.getAllocationFunction(function->getName());
    if (type != AllocationFunction::NONE) {
        if (type == AllocationFunction::REALLOC)
            return createRealloc(CInst);
        else
            return createDynAlloc(CInst, type);
    } else {
        return createUndefinedCall(CInst);
    }

    assert(false && "Unreachable");
    abort();
}

RWNode *LLVMReadWriteGraphBuilder::createPthreadCreateCalls(const llvm::CallInst *CInst)
{
    using namespace llvm;

    RWNode *rootNode = create(RWNodeType::FORK);
    auto iterator = nodes_map.find(CInst);
    if (iterator == nodes_map.end()) {
        addNode(CInst, rootNode);
    } else {
        assert(iterator->second->getType() == RWNodeType::CALL && "Adding node we already have");
        addArtificialNode(CInst, rootNode);
    }
    threadCreateCalls.emplace(CInst, rootNode);

    Value *calledValue = CInst->getArgOperand(2);
    const auto& functions = getCalledFunctions(calledValue, PTA);

    for (const Function *function : functions) {
        if (function->isDeclaration()) {
            llvm::errs() << "[RD] error: phtread_create spawns undefined function: "
                         << function->getName() << "\n";
            continue;
        }
        auto subg = getOrCreateSubgraph(function);
        makeEdge(rootNode, subg->entry->nodes.front());
    }
    return rootNode;
}

RWNode *LLVMReadWriteGraphBuilder::createPthreadJoinCall(const llvm::CallInst *CInst)
{
    // TODO later change this to create join node and set data correctly
    // we need just to create one node;
    // undefined call is overapproximation, so its ok
    RWNode *node = createUndefinedCall(CInst);
    threadJoinCalls.emplace(CInst, node);
    return node;
}

RWNode *LLVMReadWriteGraphBuilder::createPthreadExitCall(const llvm::CallInst *CInst)
{
    return createReturn(CInst);
}

ReadWriteGraph&& LLVMReadWriteGraphBuilder::build()
{
    // get entry function
    llvm::Function *F = M->getFunction(_options.entryFunction);
    if (!F) {
        llvm::errs() << "The function '" << _options.entryFunction
                     << "' was not found in the module\n";
        abort();
    }

    // first we must build globals, because nodes can use them as operands
    auto glob = buildGlobals();

    // now we can build the rest of the stuff
    auto& subg = buildFunction(*F);
    assert(subg.entry && "Do not have an entry block of the entry function");
    assert(!subg.entry->nodes.empty() && "The entry block is empty");

    RWNode *root = subg.entry->nodes.front();

    // Do we have any globals at all?
    // If so, insert them at the begining of the graph.
    if (glob.first) {
        assert(glob.second && "Have the start but not the end");
        // this is a sequence of global nodes,
        // make it the root of the graph
        makeEdge(glob.second, root);
        root = glob.first;
    }

    // Add interprocedural edges. We do that here after all functions
    // are build to avoid problems with recursive procedures and such.
    for (auto& it : calls) {
        auto callNode = it.first.first;
        auto returnNode = it.first.second;
        assert(returnNode && "Do not have return node for a call");
        assert(returnNode->getType() == RWNodeType::CALL_RETURN && "Do not have return node for a call");

        for (auto subg : it.second) {
            makeEdge(callNode, subg->entry->nodes.front());
            if (!subg->returns.empty()) {
                for (auto ret : subg->returns) {
                    makeEdge(ret, returnNode);
                }
            }

        }
    }

    if (_options.threads) {
        matchForksAndJoins();
    }

    graph.setEntry(root);

    // we must perform this because the sparse algorithm assumes
    // that every node has a block and that is the case
    // only when we have no dead code
    //graph.eliminateDeadCode();

    return std::move(graph);
}

std::pair<RWNode *, RWNode *> LLVMReadWriteGraphBuilder::buildGlobals()
{
    RWNode *cur = nullptr, *prev, *first = nullptr;
    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        prev = cur;

        // every global node is like memory allocation
        cur = create(RWNodeType::ALLOC);
        addNode(&*I, cur);

        // add the initial global definitions
        if (auto GV = llvm::dyn_cast<llvm::GlobalVariable>(&*I)) {
            auto size = llvmutils::getAllocatedSize(GV->getType()->getContainedType(0),
                                                    &M->getDataLayout());
            if (size == 0)
                size = Offset::UNKNOWN;

            cur->addDef(cur, 0, size, true /* strong update */);
        }

        if (prev)
            makeEdge(prev, cur);
        else
            first = cur;
    }

    assert((!first && !cur) || (first && cur));
    return std::pair<RWNode *, RWNode *>(first, cur);
}

///
// Map pointers of 'val' to def-sites.
// \param where  location in the program, for debugging
// \param size is the number of bytes used from the memory
std::vector<DefSite> LLVMReadWriteGraphBuilder::mapPointers(const llvm::Value *where,
                                                const llvm::Value *val,
                                                Offset size)
{
    std::vector<DefSite> result;

    auto psn = PTA->getLLVMPointsToChecked(val);
    if (!psn.first) {
        result.push_back(DefSite(UNKNOWN_MEMORY));
#ifndef NDEBUG
        llvm::errs() << "[RD] warning at: " << ValInfo(where) << "\n";
        llvm::errs() << "No points-to set for: " << ValInfo(val) << "\n";
#endif
        // don't have points-to information for used pointer
        return result;
    }

    if (psn.second.empty()) {
#ifndef NDEBUG
        llvm::errs() << "[RD] warning at: " << ValInfo(where) << "\n";
        llvm::errs() << "Empty points-to set for: " << ValInfo(val) << "\n";
#endif
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
        return result;
    }

    result.reserve(psn.second.size());

    if (psn.second.hasUnknown()) {
        result.push_back(DefSite(UNKNOWN_MEMORY));
    }

    for (const auto& ptr: psn.second) {
        if (llvm::isa<llvm::Function>(ptr.value))
            continue;

        RWNode *ptrNode = getOperand(ptr.value);
        if (!ptrNode) {
            // keeping such set is faster then printing it all to terminal
            // ... and we don't flood the terminal that way
            static std::set<const llvm::Value *> warned;
            if (warned.insert(ptr.value).second) {
                llvm::errs() << "[RD] error at "  << ValInfo(where) << "\n";
                llvm::errs() << "[RD] error for " << ValInfo(val) << "\n";
                llvm::errs() << "[RD] error: Cannot find node for "
                             << ValInfo(ptr.value) << "\n";
            }
            continue;
        }

        // FIXME: we should pass just size to the DefSite ctor, but the old code relies
        // on the behavior that when offset is unknown, the length is also unknown.
        // So for now, mimic the old code. Remove it once we fix the old code.
        result.push_back(DefSite(ptrNode, ptr.offset, ptr.offset.isUnknown() ? Offset::UNKNOWN : size));
    }

    return result;
}

} // namespace dda
} // namespace dg

