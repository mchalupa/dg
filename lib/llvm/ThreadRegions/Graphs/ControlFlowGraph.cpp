#include "dg/llvm/ThreadRegions/ControlFlowGraph.h"

#include "CriticalSectionsBuilder.h"
#include "GraphBuilder.h"
#include "ThreadRegionsBuilder.h"

ControlFlowGraph::ControlFlowGraph(dg::DGLLVMPointerAnalysis *pointsToAnalysis)
        : graphBuilder(new GraphBuilder(pointsToAnalysis)),
          threadRegionsBuilder(new ThreadRegionsBuilder()),
          criticalSectionsBuilder(new CriticalSectionsBuilder()) {}

std::set<const llvm::CallInst *> ControlFlowGraph::getJoins() const {
    return graphBuilder->getJoins();
}

std::set<const llvm::CallInst *>
ControlFlowGraph::getCorrespondingForks(const llvm::CallInst *callInst) const {
    return graphBuilder->getCorrespondingForks(callInst);
}

std::set<const llvm::CallInst *> ControlFlowGraph::getLocks() const {
    return criticalSectionsBuilder->locks();
}

std::set<const llvm::CallInst *> ControlFlowGraph::getCorrespongingUnlocks(
        const llvm::CallInst *callInst) const {
    return criticalSectionsBuilder->correspondingUnlocks(callInst);
}

std::set<const llvm::Instruction *>
ControlFlowGraph::getCorrespondingCriticalSection(
        const llvm::CallInst *callInst) const {
    return criticalSectionsBuilder->correspondingNodes(callInst);
}

ControlFlowGraph::~ControlFlowGraph() = default;

void ControlFlowGraph::buildFunction(const llvm::Function *function) {
    auto nodeSeq = graphBuilder->buildFunction(function);
    graphBuilder->matchForksAndJoins();
    graphBuilder->matchLocksAndUnlocks();

    auto locks = graphBuilder->getLocks();

    for (auto *lock : locks) {
        criticalSectionsBuilder->buildCriticalSection(lock);
    }

    threadRegionsBuilder->reserve(graphBuilder->size());
    threadRegionsBuilder->build(nodeSeq.first);
}

void ControlFlowGraph::printWithRegions(std::ostream &ostream) const {
    ostream << "digraph \"Control Flow Graph\" {\n";
    ostream << "compound = true\n";

    threadRegionsBuilder->printNodes(ostream);
    graphBuilder->printEdges(ostream);
    threadRegionsBuilder->printEdges(ostream);

    ostream << "}\n";
}

void ControlFlowGraph::printWithoutRegions(std::ostream &ostream) const {
    graphBuilder->print(ostream);
}

std::set<ThreadRegion *> ControlFlowGraph::threadRegions() {
    return threadRegionsBuilder->threadRegions();
}
