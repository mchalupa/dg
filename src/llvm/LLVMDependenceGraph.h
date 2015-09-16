/// XXX add licence
//

#ifndef _LLVM_DEPENDENCE_GRAPH_H_
#define _LLVM_DEPENDENCE_GRAPH_H_

#ifndef HAVE_LLVM
#error "Need LLVM"
#endif

#ifndef ENABLE_CFG
#error "Need CFG enabled"
#endif

#include <map>
#include <unordered_map>

// forward declaration of llvm classes
namespace llvm {
    class Module;
    class Value;
    class Function;
} // namespace llvm

#include "LLVMNode.h"
#include "DependenceGraph.h"

namespace dg {

typedef dg::BBlock<LLVMNode> LLVMBBlock;

/// ------------------------------------------------------------------
//  -- LLVMDependenceGraph
/// ------------------------------------------------------------------
class LLVMDependenceGraph : public DependenceGraph<LLVMNode>
{
public:
    LLVMDependenceGraph() : gather_callsites(nullptr) {}

    // free all allocated memory and unref subgraphs
    virtual ~LLVMDependenceGraph();

    // build a DependenceGraph from module. This method will
    // build all subgraphs (called procedures). If entry is nullptr,
    // then this methods looks for function named 'main'.
    bool build(llvm::Module *m, const llvm::Function *entry = nullptr);

    // build DependenceGraph for a function. This will automatically
    // build subgraphs of called functions
    bool build(const llvm::Function *func);

    llvm::Module *getModule() const { return module; }
    std::unordered_map<const llvm::BasicBlock *,
                       LLVMBBlock *> getConstructedBlocks()
    {
        return constructedBlocks;
    }

    // if we want to slice according some call-site(s),
    // we can gather the relevant call-sites while building
    // graph and do not need to recursively find in the graph
    // later. This can handle only direct-calls though. If the
    // function is called via pointer, it won't be covered by this
    // function
    void gatherCallsites(const char *name, std::set<LLVMNode *> *callSites)
    {
        gather_callsites = name;
        gatheredCallsites = callSites;
    }

    // go through the graph and find all (possible) call-sites
    // for a function
    bool getCallSites(const char *name, std::set<LLVMNode *> *callsites);

    // build subgraph for a call node
    LLVMDependenceGraph *buildSubgraph(LLVMNode *node);
    LLVMDependenceGraph *buildSubgraph(LLVMNode *node, const llvm::Function *);

    void computePostDominators(bool addPostDomFrontiers = false);

    bool verify() const;

    /* virtual */
    void setSlice(uint64_t sid)
    {
        DependenceGraph<LLVMNode>::setSlice(sid);
        LLVMNode *entry = getEntry();
        assert(entry);

        // mark even entry node, call-sites are
        // control dependent on it
        entry->setSlice(sid);
    }

private:
    // add formal parameters of the function to the graph
    // (graph is a graph of one procedure)
    void addFormalParameters();

    // take action specific to given instruction (while building
    // the graph). This is like if the val is call-site, build
    // subgraph or if it is a pointer-handling instr. then
    // add PSS edges etc.
    void handleInstruction(const llvm::Value *val, LLVMNode *node);

    // convert llvm basic block to our basic block
    // That includes creating all the nodes and adding them
    // to this graph and creating the basic block and
    // setting first and last instructions
    LLVMBBlock *build(const llvm::BasicBlock& BB);

    // gather call-sites of functions with given name
    // when building the graph
    std::set<LLVMNode *> *gatheredCallsites;
    const char *gather_callsites;

    std::unordered_map<const llvm::BasicBlock *, LLVMBBlock *> constructedBlocks;

    // when we want to slice according to some criterion,
    // we may gather the call-sites (good points for criterions)
    // while building the graph
    llvm::Module *module;

    // verifier needs access to private elements
    friend class LLVMDGVerifier;
};

const std::map<const llvm::Value *, LLVMDependenceGraph *>& getConstructedFunctions();

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
