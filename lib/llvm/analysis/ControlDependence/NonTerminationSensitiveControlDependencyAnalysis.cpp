#include "NonTerminationSensitiveControlDependencyAnalysis.h"
#include "Function.h"
#include "Block.h"

#include <llvm/IR/Module.h>

#include <algorithm>
#include <iostream>

using namespace std;

namespace dg {
namespace cd {

NonTerminationSensitiveControlDependencyAnalysis::NonTerminationSensitiveControlDependencyAnalysis(const llvm::Function *function, dg::LLVMPointerAnalysis *pointsToAnalysis)
    :entryFunction(function), graphBuilder(pointsToAnalysis)
{}

void NonTerminationSensitiveControlDependencyAnalysis::computeDependencies() {
    if (!entryFunction) {
        std::cerr << "Missing entry function!\n";
        return;
    }

    graphBuilder.buildFunctionRecursively(entryFunction);
    auto functions = graphBuilder.functions();

    for (auto function : functions) {
        auto nodes = function.second->nodes();
        auto condNodes = function.second->condNodes();
        map<pair<Block *, Block *>, set<pair<Block *, Block *>>> matrix;
        set<Block *> workBag;

        //(1) Initialize
        for (auto condNode : condNodes) {
            for (auto successor : condNode->successors()) {
                matrix[{successor, condNode}].insert({condNode, successor});
                workBag.insert(successor);
            }
        }

        //(2) Calculate all-path reachability
        while (!workBag.empty()) {
            auto node = *workBag.begin();
            workBag.erase(workBag.begin());
            //(2.1) One successor case
            if (node->successors().size() == 1 && node->successors().find(node) == node->successors().end()) {
                auto successor = *node->successors().begin();
                for (auto condNode : condNodes) {
                    auto &s1 = matrix[{node, condNode}];
                    auto &s2 = matrix[{successor, condNode}];
                    set<pair<Block *, Block *>> difference;
                    set_difference(s1.begin(), s1.end(),
                                   s2.begin(), s2.end(),
                                   inserter(difference, difference.begin()));
                    if (!difference.empty()) {
                        s2.insert(difference.begin(), difference.end());
                        workBag.insert(successor);
                    }
                }
            }
            //(2.2) Multiple successors case
            if (node->successors().size() > 1) {
                for (auto m : nodes) {
                    auto val = matrix[{m,node}].size();
                    if (matrix[{m,node}].size() == node->successors().size()) {
                        for (auto condNode : condNodes) {
                            if (node != condNode) {
                                auto &s1 = matrix[{node, condNode}];
                                auto &s2 = matrix[{m, condNode}];
                                set<pair<Block *, Block *>> difference;
                                set_difference(s1.begin(), s1.end(),
                                               s2.begin(), s2.end(),
                                               inserter(difference, difference.begin()));
                                if (!difference.empty()) {
                                    s2.insert(difference.begin(), difference.end());
                                    workBag.insert(m);
                                }
                            }
                        }
                    }
                }
            }
        }
        //(3) Calculate non-termination sensitive control dependence
        for (auto node : nodes) {
            for (auto condNode : condNodes) {
                auto size = matrix[{node, condNode}].size();
                if (size > 0 && size < condNode->successors().size()) {
                    controlDependency[condNode].insert(node);
                }
            }
        }

        // add interprocedural dependencies
        for (auto node : nodes) {
            if (!node->callees().empty() || !node->joins().empty()) {
                auto iterator = std::find_if(node->successors().begin(),
                                             node->successors().end(),
                                             [](const Block *block){
                                                    return block->isCallReturn();
                                             });
                if (iterator != node->successors().end()) {
                    for (auto callee : node->callees()) {
                        controlDependency[callee.second->exit()].insert(*node->successors().begin());
                    }
                    for (auto join : node->joins()) {
                        controlDependency[join.second->exit()].insert(*node->successors().begin());
                    }
                }
            }
        }
    }
}

void NonTerminationSensitiveControlDependencyAnalysis::dump(ostream &ostream) const {
    ostream << "digraph \"BlockGraph\" {\n";
    graphBuilder.dumpNodes(ostream);
    graphBuilder.dumpEdges(ostream);
    dumpDependencies(ostream);
    ostream << "}\n";
}

void NonTerminationSensitiveControlDependencyAnalysis::dumpDependencies(ostream &ostream) const {
    for (auto keyValue : controlDependency) {
        for (auto dependent : keyValue.second) {
            ostream << keyValue.first->dotName() << " -> " << dependent->dotName()
                    << " [color=blue, constraint=false]\n";
        }
    }
}


}
}
