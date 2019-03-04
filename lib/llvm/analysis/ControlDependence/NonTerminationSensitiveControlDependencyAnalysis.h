#ifndef NONTERMINATIONSENSITIVECONTROLDEPENDENCYANALYSIS_H
#define NONTERMINATIONSENSITIVECONTROLDEPENDENCYANALYSIS_H

#include "GraphBuilder.h"

#include <set>
#include <map>

#include "Block.h"

namespace llvm {
class Function;
}

namespace dg {
namespace cd {

class NonTerminationSensitiveControlDependencyAnalysis
{
public:

    NonTerminationSensitiveControlDependencyAnalysis(const llvm::Function *function, LLVMPointerAnalysis * pointsToAnalysis);

    void computeDependencies();

    void dump(std::ostream & ostream) const;

    void dumpDependencies(std::ostream & ostream) const;

    const std::map<Block *, std::set<Block *>> & controlDependencies() const { return controlDependency; }


private:
    const llvm::Function * entryFunction;
    GraphBuilder graphBuilder;
    std::map<Block *, std::set<Block *>> controlDependency;
};

}
}

#endif // NONTERMINATIONSENSITIVECONTROLDEPENDENCYANALYSIS_H
