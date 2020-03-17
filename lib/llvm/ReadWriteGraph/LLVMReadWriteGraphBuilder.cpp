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

///
// Map pointers of 'val' to def-sites.
// \param where  location in the program, for debugging
// \param size is the number of bytes used from the memory
std::vector<DefSite>
LLVMReadWriteGraphBuilder::mapPointers(const llvm::Value *where,
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
        result.push_back(DefSite(ptrNode, ptr.offset,
                                 ptr.offset.isUnknown() ?
                                    Offset::UNKNOWN : size));
    }

    return result;
}

static inline void makeEdge(RWNode *src, RWNode *dst)
{
    assert(src != nullptr);
    src->addSuccessor(dst);
}

RWNode *LLVMReadWriteGraphBuilder::getOperand(const llvm::Value *val) {
    auto *op = getNode(val);
    if (!op) {
        // lazily create allocations as these are targets in defsites
        // and may not have been created yet
        if (llvm::isa<llvm::AllocaInst>(val) ||
            // FIXME: check that it is allocation
            llvm::isa<llvm::CallInst>(val)) {
            llvm::errs() << "On demand:\n";
            op = buildNode(val).getRepresentant();
        }

        if (!op) {
            llvm::errs() << "[RWG] error: cannot find an operand: "
                         << *val << "\n";
            abort();
        }
    }
    assert(op && "Do not have an operand");
    return op;
}

/*
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
*/
#if 0
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
#endif

} // namespace dda
} // namespace dg

