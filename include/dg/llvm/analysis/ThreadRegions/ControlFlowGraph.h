#ifndef CONTROLFLOWGRAPH_H
#define CONTROLFLOWGRAPH_H

#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>

#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"

#include <map>
#include <memory>
#include <ostream>

#include "ThreadRegion.h"

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

    void traverse();

    std::set<ThreadRegion *> threadRegions() const;

    std::set<const llvm::CallInst *>
    getForks();

    std::set<const llvm::CallInst *>
    getJoins();

    std::set<const llvm::CallInst *>
    getCorrespondingForks(const llvm::CallInst * join);

    std::set<const llvm::CallInst *>
    getCorrespondingJoins(const llvm::CallInst * fork);

    friend std::ostream & operator<<(std::ostream & ostream, ControlFlowGraph & controlFlowGraph);

private:
    void connectForksWithJoins();

    friend class FunctionGraph;
    friend class BlockGraph;
    friend class ThreadRegion;
};

#endif // CONTROLFLOWGRAPH_H
