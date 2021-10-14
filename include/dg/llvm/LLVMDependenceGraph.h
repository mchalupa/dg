#ifndef LLVM_DEPENDENCE_GRAPH_H_
#define LLVM_DEPENDENCE_GRAPH_H_

#include <map>
#include <unordered_map>

#include "dg/llvm/ControlDependence/LLVMControlDependenceAnalysisOptions.h"
#include "dg/llvm/ThreadRegions/ControlFlowGraph.h"

// forward declaration of llvm classes
namespace llvm {
class Module;
class Value;
class Function;
} // namespace llvm

#include "dg/DependenceGraph.h"
#include "dg/llvm/LLVMNode.h"

namespace dg {

class LLVMPointerAnalysis;

// namespace llvmdg {
// class LLVMControlDependenceAnalysis;
//}

// FIXME: why PTA is only in the namespace dg
// and this is that nested? Make it consistent...

namespace dda {
class LLVMDataDependenceAnalysis;
}

using dda::LLVMDataDependenceAnalysis;
// using llvmdg::LLVMControlDependenceAnalysis;

using LLVMBBlock = dg::BBlock<LLVMNode>;

/// ------------------------------------------------------------------
//  -- LLVMDependenceGraph
/// ------------------------------------------------------------------
class LLVMDependenceGraph : public DependenceGraph<LLVMNode> {
    // our artificial unified exit block
    std::unique_ptr<LLVMBBlock> unifiedExitBB{};
    llvm::Function *entryFunction{nullptr};

  public:
    LLVMDependenceGraph(bool threads = false) : threads(threads) {}

    // free all allocated memory and unref subgraphs
    ~LLVMDependenceGraph() override;

    // build a nodes and CFG edges from module.
    // This method will build also all subgraphs. If entry is nullptr,
    // then this methods looks for function named 'main'.
    // NOTE: this methods does not compute the dependence edges.
    // For that functionality check the LLVMDependenceGraphBuilder.
    bool build(llvm::Module *m, llvm::Function *entry = nullptr);
    bool build(llvm::Module *m, LLVMPointerAnalysis *pts = nullptr,
               LLVMDataDependenceAnalysis *rda = nullptr,
               llvm::Function *entry = nullptr);

    // build DependenceGraph for a function. This will automatically
    // build subgraphs of called functions
    bool build(llvm::Function *func);

    LLVMDGParameters *getOrCreateParameters();
    LLVMNode *getOrCreateNoReturn();
    LLVMNode *getOrCreateNoReturn(LLVMNode *call);
    LLVMNode *getNoReturn() {
        auto *params = getParameters();
        return params ? params->getNoReturn() : nullptr;
    }

    bool addFormalParameter(llvm::Value *val);
    bool addFormalGlobal(llvm::Value *val);

    llvm::Module *getModule() const { return module; }

    // if we want to slice according some call-site(s),
    // we can gather the relevant call-sites while building
    // graph and do not need to recursively find in the graph
    // later. This can handle only direct-calls though. If the
    // function is called via pointer, it won't be covered by this
    // function
    void gatherCallsites(const char *name, std::set<LLVMNode *> *callSites) {
        gather_callsites = name;
        gatheredCallsites = callSites;
    }

    // go through the graph and find all (possible) call-sites
    // for a function
    // FIXME: can implement via getCallNodes
    bool getCallSites(const char *name, std::set<LLVMNode *> *callsites);
    // this method takes NULL-terminated array of names
    static bool getCallSites(const char *names[],
                             std::set<LLVMNode *> *callsites);
    static bool getCallSites(const std::vector<std::string> &names,
                             std::set<LLVMNode *> *callsites);

    // FIXME we need remove the callsite from here if we slice away
    // the callsite
    const std::set<LLVMNode *> &getCallNodes() const { return callNodes; }
    std::set<LLVMNode *> &getCallNodes() { return callNodes; }
    bool addCallNode(LLVMNode *c) { return callNodes.insert(c).second; }

    // build subgraph for a call node
    LLVMDependenceGraph *buildSubgraph(LLVMNode *node);
    LLVMDependenceGraph *buildSubgraph(LLVMNode *node,
                                       llvm::Function * /*callFunc*/,
                                       bool fork = false);
    void addSubgraphGlobalParameters(LLVMDependenceGraph *subgraph);

    static void addNoreturnDependencies(LLVMNode *noret, LLVMBBlock *from);
    void
    addNoreturnDependencies(const LLVMControlDependenceAnalysisOptions &opts);

    void computeControlDependencies(
            const LLVMControlDependenceAnalysisOptions &opts);

    bool verify() const;

    void setThreads(bool threads);

    /* virtual */
    void setSlice(uint64_t sid) override {
        DependenceGraph<LLVMNode>::setSlice(sid);
        LLVMNode *entry = getEntry();
        assert(entry);

        // mark even entry node, call-sites are
        // control dependent on it
        entry->setSlice(sid);
    }

    LLVMPointerAnalysis *getPTA() const { return PTA; }
    LLVMDataDependenceAnalysis *getDDA() const { return DDA; }

    LLVMNode *findNode(llvm::Value *value) const;

    void addDefUseEdges(bool preserveDbg = true);
    void computeInterferenceDependentEdges(ControlFlowGraph *controlFlowGraph);
    static void computeForkJoinDependencies(ControlFlowGraph *controlFlowGraph);
    static void computeCriticalSections(ControlFlowGraph *controlFlowGraph);

  private:
    void computePostDominators(bool addPostDomFrontiers = false);
    void computeNonTerminationControlDependencies();
    void computeNTSCD(const LLVMControlDependenceAnalysisOptions &opts);

    void computeInterferenceDependentEdges(
            const std::set<const llvm::Instruction *> &loads,
            const std::set<const llvm::Instruction *> &stores);

    std::set<const llvm::Instruction *> getLoadInstructions(
            const std::set<const llvm::Instruction *> &llvmInstructions) const;
    std::set<const llvm::Instruction *> getStoreInstructions(
            const std::set<const llvm::Instruction *> &llvmInstructions) const;

    static std::set<const llvm::Instruction *> getInstructionsOfType(
            unsigned opCode,
            const std::set<const llvm::Instruction *> &llvmInstructions);

    // add formal parameters of the function to the graph
    // (graph is a graph of one procedure)
    void addFormalParameters();

    // take action specific to given instruction (while building
    // the graph). This is like if the value is a call-site,
    // then build subgraph or similar
    void handleInstruction(llvm::Value *val, LLVMNode *node,
                           LLVMNode *prevNode);

    // convert llvm basic block to our basic block
    // That includes creating all the nodes and adding them
    // to this graph and creating the basic block and
    // setting first and last instructions
    LLVMBBlock *build(llvm::BasicBlock &BB);

    // gather call-sites of functions with given name
    // when building the graph
    std::set<LLVMNode *> *gatheredCallsites;
    const char *gather_callsites{nullptr};

    bool threads{false};

    // all callnodes in this graph - forming call graph
    std::set<LLVMNode *> callNodes;

    // when we want to slice according to some criterion,
    // we may gather the call-sites (good points for criterions)
    // while building the graph
    llvm::Module *module{nullptr};

    // analyses needed for building the graph
    LLVMPointerAnalysis *PTA{nullptr};
    LLVMDataDependenceAnalysis *DDA;
    // LLVMControlDependenceAnalysis *CDA;

    // verifier needs access to private elements
    friend class LLVMDGVerifier;
};

const std::map<llvm::Value *, LLVMDependenceGraph *> &getConstructedFunctions();

LLVMNode *findInstruction(llvm::Instruction *instruction,
                          const std::map<llvm::Value *, LLVMDependenceGraph *>
                                  &constructedFunctions);

llvm::Instruction *castToLLVMInstruction(const llvm::Value *value);
} // namespace dg

#endif // DEPENDENCE_GRAPH_H_
