#include "NTSCD.h"
#include "Function.h"
#include "Block.h"

#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Module.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

namespace llvm {
    class Function;
}

#include <algorithm>
#include <functional>
#include <queue>
#include <unordered_set>

#include "dg/util/debug.h"

using namespace std;

namespace dg {
namespace llvmdg {

NTSCD::NTSCD(const llvm::Module *module,
             const LLVMControlDependenceAnalysisOptions& opts,
             dg::LLVMPointerAnalysis *pointsToAnalysis)
    : LLVMControlDependenceAnalysisImpl(module, opts), graphBuilder(pointsToAnalysis)
{}

// a is CD on b
void NTSCD::addControlDependence(Block *a, Block *b) {
    controlDependency[b].insert(a);
    revControlDependency[a].insert(b);
}

// FIXME: make it working on-demand
void NTSCD::computeDependencies(Function *function) {
    DBG_SECTION_BEGIN(cda, "Computing CD for a function");
    auto nodes = function->nodes();
    auto condNodes = function->condNodes();
    auto callReturnNodes = function->callReturnNodes();

    DBG_SECTION_BEGIN(cda, "Computing intraprocedural CD");
    for (auto node : nodes) {
        // (1) initialize
        nodeInfo.clear();
        nodeInfo.reserve(nodes.size());
        for (auto node1 : nodes) {
            nodeInfo[node1].outDegreeCounter = node1->successors().size();
        }
        // (2) traverse
        visitInitialNode(node);
        // (3) find out dependencies
        for (auto node1 : nodes) {
            if (hasRedAndNonRedSuccessor(node1)) {
                // node is CD on node1
                addControlDependence(node, node1);
            }
        }

    }
    DBG_SECTION_END(cda, "Finished computing intraprocedural CD");

    // add interprocedural dependencies
    if (getOptions().interproceduralCD()) {
        DBG_SECTION_BEGIN(cda, "Computing interprocedural CD");
        for (auto node : nodes) {
            if (!node->callees().empty() || !node->joins().empty()) {
                auto iterator = std::find_if(node->successors().begin(),
                                             node->successors().end(),
                                             [](const Block *block){
                                                    return block->isCallReturn();
                                             });
                if (iterator != node->successors().end()) {
                    for (auto callee : node->callees()) {
                        addControlDependence(*iterator, callee.second->exit());
                    }
                    for (auto join : node->joins()) {
                        addControlDependence(*iterator, join.second->exit());
                    }
                }
            }
        }

        for (auto node : callReturnNodes) {
            std::queue<Block *> q;
            std::unordered_set<Block *> visited(nodes.size());
            visited.insert(node);
            for(auto successor : node->successors()) {
                if (visited.find(successor) == visited.end()) {
                    q.push(successor);
                    visited.insert(successor);
                }
            }
            while (!q.empty()) {
                addControlDependence(q.front(), node);
                for(auto successor : q.front()->successors()) {
                    if (visited.find(successor) == visited.end()) {
                        q.push(successor);
                        visited.insert(successor);
                    }
                }
                q.pop();
            }
        }
        DBG_SECTION_END(cda, "Finished computing interprocedural CD");
    }
    DBG_SECTION_END(cda, "Finished computing CD for a function");
}

void NTSCD::computeDependencies() {
    DBG_SECTION_BEGIN(cda, "Computing CD for the whole module");
    auto *entryFunction = getModule()->getFunction(getOptions().entryFunction);
    if (!entryFunction) {
        llvm::errs() << "Missing entry function: "
                     << getOptions().entryFunction << "\n";
        return;
    }

    graphBuilder.buildFunctionRecursively(entryFunction);
    auto entryFunc = graphBuilder.findFunction(entryFunction);

    entryFunc->entry()->visit();

    for (auto& it : graphBuilder.functions()) {
        computeDependencies(it.second);
    }
    DBG_SECTION_END(cda, "Finished computing CD for the whole module");
}

void NTSCD::dump(ostream &ostream) const {
    ostream << "digraph \"BlockGraph\" {\n";
    graphBuilder.dumpNodes(ostream);
    graphBuilder.dumpEdges(ostream);
    dumpDependencies(ostream);
    ostream << "}\n";
}

void NTSCD::dumpDependencies(ostream &ostream) const {
    for (auto keyValue : controlDependency) {
        for (auto dependent : keyValue.second) {
            ostream << keyValue.first->dotName() << " -> " << dependent->dotName()
                    << " [color=blue, constraint=false]\n";
        }
    }
}

void NTSCD::visitInitialNode(Block *node) {
    nodeInfo[node].red = true;
    for (auto predecessor : node->predecessors()) {
        visit(predecessor);
    }
}

void NTSCD::visit(Block *node) {
    if (nodeInfo[node].outDegreeCounter == 0) {
        return;
    }
    nodeInfo[node].outDegreeCounter--;
    if (nodeInfo[node].outDegreeCounter == 0) {
        nodeInfo[node].red = true;
        for (auto predecessor : node->predecessors()) {
            visit(predecessor);
        }
    }
}

bool NTSCD::hasRedAndNonRedSuccessor(Block *node) {
    size_t redCounter = 0;
    for (auto successor : node->successors()) {
        if (nodeInfo[successor].red) {
            ++redCounter;
        }
    }
    return redCounter > 0 && node->successors().size() > redCounter;
}

} // namespace llvmdg
} // namespace dg
