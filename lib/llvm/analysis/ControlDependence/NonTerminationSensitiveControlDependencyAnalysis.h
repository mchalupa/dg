#ifndef NONTERMINATIONSENSITIVECONTROLDEPENDENCYANALYSIS_H
#define NONTERMINATIONSENSITIVECONTROLDEPENDENCYANALYSIS_H

#include "GraphBuilder.h"

#include <set>
#include <map>

namespace llvm {
class Module;
}

namespace dg {
namespace cd {

class Block;

class NonTerminationSensitiveControlDependencyAnalysis
{
public:

    NonTerminationSensitiveControlDependencyAnalysis(const llvm::Module * module,LLVMPointerAnalysis * pointsToAnalysis);

    void computeDependencies();

    void dump(std::ostream & ostream) const;

    void dumpDependencies(std::ostream & ostream) const;


private:
    const llvm::Module * module;
    GraphBuilder graphBuilder;
    std::map<Block *, std::set<Block *>> controlDependency;
};

}
}

#endif // NONTERMINATIONSENSITIVECONTROLDEPENDENCYANALYSIS_H
