/// XXX add licence
//

#ifndef HAVE_LLVM
# error "Need LLVM for LLVMDependenceGraph"
#endif

#ifndef ENABLE_CFG
 #error "Need CFG enabled for building LLVM Dependence Graph"
#endif

#include <utility>
#include <unordered_map>
#include <set>

#include <llvm/Analysis/PostDominators.h>
#include <llvm/Analysis/DominanceFrontier.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/CFG.h>
#include <llvm/Support/raw_ostream.h>

#include "Utils.h"
#include "LLVMDependenceGraph.h"
#include "LLVMNode.h"

#include "PointsTo.h"
#include "DefUse.h"

#include "llvm-debug.h"

using llvm::errs;
using std::make_pair;

namespace dg {

/// ------------------------------------------------------------------
//  -- LLVMDependenceGraph
/// ------------------------------------------------------------------

LLVMDependenceGraph::~LLVMDependenceGraph()
{
    // delete nodes
    for (auto I = begin(), E = end(); I != E; ++I) {
        LLVMNode *node = I->second;

        if (node) {
            for (auto subgraph : node->getSubgraphs()) {
                // graphs are referenced, once the refcount is 0
                // the graph will be deleted
                // Because of recursive calls, graph can be its
                // own subgraph. In that case we're in the destructor
                // already, so do not delete it
                    subgraph->unref(subgraph != this);
            }

            LLVMDGParameters *params = node->getParameters();
            if (params) {
                for (auto par : *params) {
                    delete par.second.in;
                    delete par.second.out;
                }

                delete params;
            }

            if (!node->getBasicBlock()
                && !llvm::isa<llvm::Function>(*I->first))
                DBG("WARN: Value " << *I->first << "had no BB assigned");

            delete node;
        } else {
            DBG("WARN: Value " << *I->first << "had no node assigned");
        }
    }
}

static void addGlobals(llvm::Module *m, LLVMDependenceGraph *dg)
{
    for (const llvm::GlobalVariable& gl : m->globals())
        dg->addGlobalNode(new LLVMNode(&gl));
}

bool LLVMDependenceGraph::build(llvm::Module *m, const llvm::Function *entry)
{
    // get entry function if not given
    if (!entry)
        entry = m->getFunction("main");

    if (!entry) {
        errs() << "No entry function found/given\n";
        return false;
    }

    module = m;

    // add global nodes. These will be shared across subgraphs
    addGlobals(m, this);

    // build recursively DG from entry point
    build(entry);

    return true;
};

LLVMDependenceGraph *
LLVMDependenceGraph::buildSubgraph(LLVMNode *node)
{
    using namespace llvm;

    const Value *val = node->getValue();
    const CallInst *CInst = dyn_cast<CallInst>(val);
    assert(CInst && "buildSubgraph called on non-CallInst");
    const Function *callFunc = CInst->getCalledFunction();

    return buildSubgraph(node, callFunc);
}


LLVMDependenceGraph *
LLVMDependenceGraph::buildSubgraph(LLVMNode *node, const llvm::Function *callFunc)
{
    using namespace llvm;

    LLVMBBlock *BB;

    // if we don't have this subgraph constructed, construct it
    // else just add call edge
    LLVMDependenceGraph *&subgraph = constructedFunctions[callFunc];

    if (!subgraph) {
        // since we have reference the the pointer in
        // constructedFunctions, we can assing to it
        subgraph = new LLVMDependenceGraph();
        // set global nodes to this one, so that
        // we'll share them
        subgraph->setGlobalNodes(getGlobalNodes());
        subgraph->module = module;
        // make subgraphs gather the call-sites too
        subgraph->gatherCallsites(gather_callsites, gatheredCallsites);

        // make the real work
        bool ret = subgraph->build(callFunc);

        // at least for now use just assert, if we'll
        // have a reason to handle such failures at some
        // point, we can change it
        assert(ret && "Building subgraph failed");

        // we built the subgraph, so it has refcount = 1,
        // later in the code we call addSubgraph, which
        // increases the refcount to 2, but we need this
        // subgraph to has refcount 1, so unref it
        subgraph->unref(false /* deleteOnZero */);
    }

    BB = node->getBasicBlock();
    assert(BB && "do not have BB; this is a bug, sir");
    BB->addCallsite(node);

    return subgraph;
}

static bool
is_func_defined(const llvm::CallInst *CInst)
{
    llvm::Function *callFunc = CInst->getCalledFunction();

    if (!callFunc || callFunc->size() == 0)
        return false;

    return true;
}


void LLVMDependenceGraph::handleInstruction(const llvm::Value *val,
                                            LLVMNode *node)
{
    using namespace llvm;

    if (const CallInst *CInst = dyn_cast<CallInst>(val)) {
        const Function *func = CInst->getCalledFunction();
        // if func is nullptr, then this is indirect call
        // via function pointer. We cannot do something with
        // that here, we don't know the points-to
        if (func && gather_callsites &&
            strcmp(func->getName().data(),
                   gather_callsites) == 0) {
            gatheredCallsites->insert(node);
        }

        if (is_func_defined(CInst)) {
            LLVMDependenceGraph *subg = buildSubgraph(node);
            node->addSubgraph(subg);
            node->addActualParameters(subg);
        }
    }
}

LLVMBBlock *LLVMDependenceGraph::build(const llvm::BasicBlock& llvmBB)
{
    using namespace llvm;

    BasicBlock::const_iterator IT = llvmBB.begin();
    const Value *val = &(*IT);

    LLVMNode *predNode = nullptr;
    LLVMNode *node = new LLVMNode(val);
    LLVMBBlock *BB = new LLVMBBlock(node);

    addNode(node);
    handleInstruction(val, node);

    ++IT; // shift to next instruction, we have the first one handled
    predNode = node;

    // iterate over the instruction and create node for every single
    // one of them + add CFG edges
    for (BasicBlock::const_iterator Inst = IT, EInst = llvmBB.end();
         Inst != EInst; ++Inst) {

        val = &(*Inst);
        node = new LLVMNode(val);
        // add new node to this dependence graph
        addNode(node);

        // add successor to predcessor node
        if (predNode)
            predNode->setSuccessor(node);

        // set new predcessor node for next iteration
        predNode = node;

        // take instruction specific actions
        handleInstruction(val, node);
    }

    // check if this is the exit node of function
    const TerminatorInst *term = llvmBB.getTerminator();
    if (!term) {
        DBG("WARN: Basic block is not well formed\n" << llvmBB);
        return BB;
    }

    // create one unified exit node from function and add control dependence
    // to it from every return instruction. We could use llvm pass that
    // would do it for us, but then we would lost the advantage of working
    // on dep. graph that is not for whole llvm
    const ReturnInst *ret = dyn_cast<ReturnInst>(term);
    if (ret) {
        LLVMNode *ext = getExit();
        if (!ext) {
            // we need new llvm value, so that the nodes won't collide
            ReturnInst *phonyRet
                = ReturnInst::Create(ret->getContext()/*, ret->getReturnValue()*/);
            if (!phonyRet) {
                errs() << "ERR: Failed creating phony return value "
                       << "for exit node\n";
                // XXX later we could return somehow more mercifully
                abort();
            }

            ext = new LLVMNode(phonyRet);
            addNode(ext);
            setExit(ext);

            LLVMBBlock *retBB = new LLVMBBlock(ext, ext);
            setExitBB(retBB);
        }

        // add control dependence from this (return) node
        // to EXIT node
        assert(node && "BUG, no node after we went through basic block");
        node->addControlDependence(ext);
        BB->addSuccessor(getExitBB());
    }

    // set last node
    BB->setLastNode(node);

    // sanity check if we have the first and the last node set
    assert(BB->getFirstNode() && "No first node in BB");
    assert(BB->getLastNode() && "No last node in BB");

    return BB;
}

bool LLVMDependenceGraph::build(const llvm::Function *func)
{
    using namespace llvm;

    assert(func && "Passed no func");

    // do we have anything to process?
    if (func->size() == 0)
        return false;

    // create entry node
    LLVMNode *entry = new LLVMNode(func);
    addGlobalNode(entry);
    setEntry(entry);

    constructedFunctions.insert(make_pair(func, this));
    constructedBlocks.reserve(func->size());

    // add formal parameters to this graph
    addFormalParameters();

    // iterate over basic blocks
    for (const llvm::BasicBlock& llvmBB : *func) {
        LLVMBBlock *BB = build(llvmBB);
        constructedBlocks[&llvmBB] = BB;

        // first basic block is the entry BB
        if (!getEntryBB())
            setEntryBB(BB);
    }

    // add CFG edges
    for (auto it : constructedBlocks) {
        const BasicBlock *llvmBB = it.first;
        LLVMBBlock *BB = it.second;

        for (succ_const_iterator S = succ_begin(llvmBB), SE = succ_end(llvmBB);
             S != SE; ++S) {
            LLVMBBlock *succ = constructedBlocks[*S];
            assert(succ && "Missing basic block");

            BB->addSuccessor(succ);
        }
    }

    // check if we have everything
    assert(getEntry() && "Missing entry node");
    assert(getExit() && "Missing exit node");
    assert(getEntryBB() && "Missing entry BB");
    assert(getExitBB() && "Missing exit BB");

    // add CFG edge from entry point to the first instruction
    entry->addControlDependence(getEntryBB()->getFirstNode());

    return true;
}

void LLVMDependenceGraph::addFormalParameters()
{
    using namespace llvm;

    LLVMNode *entryNode = getEntry();
    assert(entryNode && "No entry node when adding formal parameters");

    const Function *func = dyn_cast<Function>(entryNode->getValue());
    assert(func && "entry node value is not a function");
    //assert(func->arg_size() != 0 && "This function is undefined?");
    if (func->arg_size() == 0)
        return;

    LLVMDGParameters *params = new LLVMDGParameters();
    setParameters(params);

    LLVMNode *in, *out;
    for (auto I = func->arg_begin(), E = func->arg_end();
         I != E; ++I) {
        const Value *val = (&*I);

        in = new LLVMNode(val);
        out = new LLVMNode(val);
        params->add(val, in, out);

        // add control edges
        entryNode->addControlDependence(in);
        entryNode->addControlDependence(out);
    }
}

} // namespace dg

namespace llvm {

// taken from llvm/Analysis/DominanceFrontiers.h and modified to our needs
class PostDominanceFrontiers : public DominanceFrontierBase<BasicBlock> {
private:
  typedef GraphTraits<BasicBlock *> BlockTraits;

public:
  typedef DominatorTreeBase<BasicBlock> DomTreeT;
  typedef DomTreeNodeBase<BasicBlock> DomTreeNodeT;
  typedef typename DominanceFrontierBase<BasicBlock>::DomSetType DomSetType;

  PostDominanceFrontiers() : DominanceFrontierBase<BasicBlock>(true /* is postdom */) {}

  void analyze(DomTreeT &DT) {
    this->Roots = DT.getRoots();
    assert(this->Roots.size() == 1 &&
           "Only one entry block for forward domfronts!");
    calculate(DT, DT[this->Roots[0]]);
  }

  template <class BlockT>
  class DFCalculateWorkObject {
  public:
    typedef DomTreeNodeBase<BlockT> DomTreeNodeT;

    DFCalculateWorkObject(BlockT *B, BlockT *P, const DomTreeNodeT *N,
                          const DomTreeNodeT *PN)
        : currentBB(B), parentBB(P), Node(N), parentNode(PN) {}
    BlockT *currentBB;
    BlockT *parentBB;
    const DomTreeNodeT *Node;
    const DomTreeNodeT *parentNode;
  };

  // this implementation is taken from DominanceFrontiersImpl.h
  const DomSetType &calculate(const DomTreeT &DT, const DomTreeNodeT *Node)
  {
    BasicBlock *BB = Node->getBlock();
    DomSetType *Result = nullptr;

    std::vector<DFCalculateWorkObject<BasicBlock>> workList;
    SmallPtrSet<BasicBlock *, 32> visited;

    workList.push_back(DFCalculateWorkObject<BasicBlock>(BB, nullptr, Node, nullptr));
    do {
      DFCalculateWorkObject<BasicBlock> *currentW = &workList.back();
      assert(currentW && "Missing work object.");

      BasicBlock *currentBB = currentW->currentBB;
      BasicBlock *parentBB = currentW->parentBB;
      const DomTreeNodeT *currentNode = currentW->Node;
      const DomTreeNodeT *parentNode = currentW->parentNode;
      assert(currentBB && "Invalid work object. Missing current Basic Block");
      assert(currentNode && "Invalid work object. Missing current Node");
      DomSetType &S = this->Frontiers[currentBB];

      // Visit each block only once.
      if (visited.insert(currentBB).second) {
        // Loop over CFG successors to calculate DFlocal[currentNode]
        for (auto SI = BlockTraits::child_begin(currentBB),
                  SE = BlockTraits::child_end(currentBB);
             SI != SE; ++SI) {
          // Does Node immediately dominate this successor?
          if (DT[*SI]->getIDom() != currentNode)
            S.insert(*SI);
        }
      }

      // At this point, S is DFlocal.  Now we union in DFup's of our children...
      // Loop through and visit the nodes that Node immediately dominates (Node's
      // children in the IDomTree)
      bool visitChild = false;
      for (typename DomTreeNodeT::const_iterator NI = currentNode->begin(),
                                                 NE = currentNode->end();
           NI != NE; ++NI) {
        DomTreeNodeT *IDominee = *NI;
        BasicBlock *childBB = IDominee->getBlock();
        if (visited.count(childBB) == 0) {
          workList.push_back(DFCalculateWorkObject<BasicBlock>(
              childBB, currentBB, IDominee, currentNode));
          visitChild = true;
        }
      }

      // If all children are visited or there is any child then pop this block
      // from the workList.
      if (!visitChild) {
        if (!parentBB) {
          Result = &S;
          break;
        }

        typename DomSetType::const_iterator CDFI = S.begin(), CDFE = S.end();
        DomSetType &parentSet = this->Frontiers[parentBB];
        for (; CDFI != CDFE; ++CDFI) {
          if (!DT.properlyDominates(parentNode, DT[*CDFI]))
            parentSet.insert(*CDFI);
        }
        workList.pop_back();
      }

    } while (!workList.empty());

    return *Result;
  }

};

} // namespace llvm

namespace dg {

void LLVMDependenceGraph::computePostDominators(bool addPostDomEdges,
                                                bool addPostDomFrontiers)
{
    using namespace llvm;
    PostDominatorTree *pdtree = new PostDominatorTree();
    for (auto F : constructedFunctions) {
        Value *val = const_cast<Value *>(F.first);
        Function& f = *cast<Function>(val);
        pdtree->runOnFunction(f);

        auto our_blocks = F.second->getConstructedBlocks();
        for (auto it : our_blocks) {
            LLVMBBlock *BB = it.second;
            BasicBlock *B = const_cast<BasicBlock *>(it.first);
            DomTreeNode *N = pdtree->getNode(B);

            if (addPostDomEdges) {
                DomTreeNode *idom = N->getIDom();
                if (idom) {
                    LLVMBBlock *pb = our_blocks[idom->getBlock()];
                    assert(pb && "Do not have constructed BB");
                    BB->setIPostDom(pb);
                }
            }

            if (addPostDomFrontiers) {
                PostDominanceFrontiers pdfrontiers;
                const PostDominanceFrontiers::DomSetType& frontiers
                    = pdfrontiers.calculate(*pdtree->DT, N);

                for (BasicBlock *llvmBB : frontiers) {
                    LLVMBBlock *f = our_blocks[llvmBB];
                    assert(f && "Do not have constructed BB");
                    BB->addPostDomFrontier(f);
                }
            }
        }
    }

    delete pdtree;
}

} // namespace dg
