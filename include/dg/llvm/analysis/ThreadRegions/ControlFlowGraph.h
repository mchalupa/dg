#ifndef CONTROLFLOWGRAPH_H
#define CONTROLFLOWGRAPH_H

#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>

#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"

#include <map>
#include <memory>
#include <ostream>

#include "ThreadRegion.h"

struct CriticalSection {
    
    CriticalSection();

    CriticalSection(const llvm::Value * lock,
                    std::set<const llvm::Value *> &&joins,
                    std::set<const llvm::Value *> &&nodes);

    const llvm::Value *             lock;
    std::set<const llvm::Value *>   unlocks;
    std::set<const llvm::Value *>   nodes;
};

inline bool operator<(const CriticalSection &lhs, const CriticalSection &rhs) {
    return lhs.lock < rhs.lock;
}

class FunctionGraph;

namespace dg {
    class LLVMPointerAnalysis;
}

class ControlFlowGraph
{
private:
    const llvm::Module *                llvmModule          = nullptr;
    const dg::LLVMPointerAnalysis *     pointsToAnalysis_   = nullptr;

    std::set<const llvm::CallInst *>    threadForks;
    std::set<const llvm::CallInst *>    threadJoins;

    std::set<const llvm::CallInst *>    locks;
    std::set<const llvm::CallInst *>    unlocks;

    std::set<ThreadRegion *>            threadRegions_;

    std::string             entryFunction;
    llvm::Function *        llvmEntryFunction   = nullptr;
    FunctionGraph *         entryFunctionGraph  = nullptr;

    std::map<const llvm::Function *, FunctionGraph *> llvmToFunctionGraphMap;

public:
    ControlFlowGraph(const llvm::Module *module,
                     const dg::LLVMPointerAnalysis *pointsToAnalysis_,
                     const std::string &entryFunction = "main");

    ~ControlFlowGraph();

    void build();

    void computeThreadRegions();

    void computeCriticalSections();

    std::set<ThreadRegion *> threadRegions() const;

    std::set<const llvm::CallInst *>
    getForks();

    std::set<const llvm::CallInst *>
    getJoins();

    std::set<const llvm::CallInst *>
    getCorrespondingForks(const llvm::CallInst * join);

    std::set<const llvm::CallInst *>
    getCorrespondingJoins(const llvm::CallInst * fork);

    std::set<CriticalSection> getCriticalSections();

    friend std::ostream & operator<<(std::ostream & ostream, ControlFlowGraph & controlFlowGraph);

private:
    void connectForksWithJoins();

    void matchLocksWithUnlocks();

    FunctionGraph * createOrGetFunctionGraph(const llvm::Function * function);
    
    FunctionGraph * findFunction(const llvm::Function * function);
    
    Node * findNode(const llvm::Value * value);

    void clearDfsState();

    friend class FunctionGraph;
    friend class BlockGraph;
    friend class ThreadRegion;
};

#endif // CONTROLFLOWGRAPH_H
