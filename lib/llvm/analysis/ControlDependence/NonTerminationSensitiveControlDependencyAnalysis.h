#ifndef DG_LLVM_NONTERMINATIONSENSITIVECONTROLDEPENDENCYANALYSIS_H
#define DG_LLVM_NONTERMINATIONSENSITIVECONTROLDEPENDENCYANALYSIS_H

#include "GraphBuilder.h"

#include <set>
#include <map>
#include <unordered_map>

#include "Block.h"

namespace llvm {
class Function;
}

namespace dg {

class LLVMPointerAnalysis;

namespace cd {

class NonTerminationSensitiveControlDependencyAnalysis
{
public:
    struct NodeInfo {
        bool visited = false;
        bool red = false;
        size_t outDegreeCounter = 0;
    };

    NonTerminationSensitiveControlDependencyAnalysis(const llvm::Function *function,
                                                     LLVMPointerAnalysis *pointsToAnalysis);

    void computeDependencies();

    void dump(std::ostream & ostream) const;

    void dumpDependencies(std::ostream & ostream) const;

    const std::map<Block *, std::set<Block *>> & controlDependencies() const { return controlDependency; }


private:
    const llvm::Function * entryFunction;
    GraphBuilder graphBuilder;
    std::map<Block *, std::set<Block *>> controlDependency;
    std::unordered_map<Block *, NodeInfo> nodeInfo;

private:
    void visitInitialNode(Block * node);

    void visit(Block * node);

    bool hasRedAndNonRedSuccessor(Block * node);
};

}
}

#endif // DG_LLVM_NONTERMINATIONSENSITIVECONTROLDEPENDENCYANALYSIS_H
