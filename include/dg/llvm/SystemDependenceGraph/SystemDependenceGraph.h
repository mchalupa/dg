#ifndef DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_
#define DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_

#include <map>

#include "dg/SystemDependenceGraph/SystemDependenceGraph.h"
//#include "SystemDependenceGraphBuilder.h"

namespace llvm {
    class Module;
    class Value;
}

namespace dg {
namespace llvmdg {

class SystemDependenceGraph {
    //const SystemDependenceGraphOptions _options;
    llvm::Module *_module;
    dg::sdg::SystemDependenceGraph _sdg;
    //SystemDependenceGraphBuilder _builder;
    std::map<const llvm::Value *, sdg::DGNode *> _mapping;

    void buildSDG();

public:
    SystemDependenceGraph(llvm::Module *M)
                          //const SystemDependenceGraphOptions& opts)
    //: _options(opts), _module(M) {}
    :  _module(M),
       _sdg() {
        buildSDG();
    }

    llvm::Module *getModule() { return _module; }
    const llvm::Module *getModule() const { return _module; }
};

} // namespace llvmdg
} // namespace dg

#endif // DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_
