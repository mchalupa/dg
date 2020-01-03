#ifndef DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_
#define DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_

#include <map>

#include "dg/SystemDependenceGraph/SystemDependenceGraph.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

namespace llvm {
    class Module;
    class Value;
}

namespace dg {
namespace llvmdg {

class SystemDependenceGraph {
    //const SystemDependenceGraphOptions _options;
    llvm::Module *_module;
    sdg::SystemDependenceGraph _sdg;
    LLVMPointerAnalysis *_pta{nullptr};

    //SystemDependenceGraphBuilder _builder;
    std::map<const llvm::Value *, sdg::DGNode *> _mapping;

    void buildSDG();

public:
    SystemDependenceGraph(llvm::Module *M,
                          LLVMPointerAnalysis *PTA)
                          //const SystemDependenceGraphOptions& opts)
    :  _module(M), _sdg(), _pta{PTA} {
        buildSDG();
    }

    llvm::Module *getModule() { return _module; }
    const llvm::Module *getModule() const { return _module; }

    void addMapping(const llvm::Value *v, sdg::DGNode *n) {
        assert(_mapping.find(v) == _mapping.end() &&
                "Already have this value");
        _mapping[v] = n;
    }

    sdg::SystemDependenceGraph& getSDG() { return _sdg; }
    const sdg::SystemDependenceGraph& getSDG() const { return _sdg; }
};

} // namespace llvmdg
} // namespace dg

#endif // DG_LLVM_SYSTEM_DEPENDNECE_GRAPH_H_
