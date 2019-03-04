#ifndef DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_
#define DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_

#include "dg/SystemDependenceGraph/SystemDependenceGraph.h"

namespace dg {
namespace llvmdg {

class SystemDependenceGraph {
    //const SystemDependenceGraphOptions _options;
    llvm::Module *_module;
    dg::SystemDependenceGraph _sdg;
    std::map<const llvm::Value *, DGNode *> _mapping;

    void buildSDG();
public:
    SystemDependenceGraph(llvm::Module *M)
                          //const SystemDependenceGraphOptions& opts)
    //: _options(opts), _module(M) {}
    :  _module(M), _builder(M), _sdg() {
        buildSDG();
    }
};

} // namespace llvmdg
} // namespace dg

#endif // DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_
