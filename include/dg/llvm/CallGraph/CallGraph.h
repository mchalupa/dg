#ifndef DG_LLVM_CALLGRAPH_H_
#define DG_LLVM_CALLGRAPH_H_

#include <vector>
#include <memory>

#include "dg/PointerAnalysis/PSNode.h"
#include "dg/CallGraph/CallGraph.h"

namespace llvm {
    class Function;
}

namespace dg {
namespace pta {
    class PSNode;
}

namespace llvmdg {

class CallGraphImpl {
public:
    using FuncVec = std::vector<const llvm::Function *>;

    virtual ~CallGraphImpl() = default;

    // XXX: return iterators...
    virtual FuncVec functions() const = 0;
    virtual FuncVec callers(const llvm::Function *) const = 0;
    virtual FuncVec callees(const llvm::Function *) const = 0;
    virtual bool calls(const llvm::Function*, const llvm::Function *) const = 0;
};

class DGCallGraphImpl : public CallGraphImpl {
    const GenericCallGraph<PSNode*>& _cg;
    std::map<const llvm::Function *, PSNode *> _mapping;

    const llvm::Function *getFunFromNode(PSNode *n) const {
        auto f =  n->getUserData<llvm::Function>();
        assert(f && "Invalid data in a node");
        return f;
    }

public:
    DGCallGraphImpl(const dg::GenericCallGraph<PSNode*>& cg) : _cg(cg) {
        for (auto& it : _cg) {
            _mapping[getFunFromNode(it.first)] = it.first;
        }
    }

    FuncVec functions() const override {
        FuncVec ret;
        for (auto& it : _mapping) {
            ret.push_back(it.first);
        }
        return ret;
    }

    FuncVec callers(const llvm::Function *F) const override {
        FuncVec ret;
        auto it = _mapping.find(F);
        if (it == _mapping.end())
            return ret;
        auto fnd = _cg.get(it->second);
        for (auto *nd : fnd->getCallers()) {
            ret.push_back(getFunFromNode(nd->getValue()));
        }
        return ret;
    }

    FuncVec callees(const llvm::Function *F) const override {
        FuncVec ret;
        auto it = _mapping.find(F);
        if (it == _mapping.end())
            return ret;
        auto fnd = _cg.get(it->second);
        for (auto *nd : fnd->getCalls()) {
            ret.push_back(getFunFromNode(nd->getValue()));
        }
        return ret;
    }

    bool calls(const llvm::Function *F, const llvm::Function *what) const override {
        auto it = _mapping.find(F);
        if (it == _mapping.end())
            return false;
        auto it2 = _mapping.find(what);
        if (it2 == _mapping.end())
            return false;
        auto fn1 =  _cg.get(it->second);
        auto fn2 =  _cg.get(it2->second);
        if (fn1 && fn2) {
            return fn1->calls(fn2);
        }
        return false;
    };

};

class CallGraph {
    std::unique_ptr<CallGraphImpl> _impl;

public:
    using FuncVec = CallGraphImpl::FuncVec;

    CallGraph(GenericCallGraph<PSNode*>& cg) : _impl(new DGCallGraphImpl(cg)) {}

    ///
    /// Get all functions in this call graph
    ///
    FuncVec functions() const { return _impl->functions(); }
    ///
    /// Get callers of a function
    ///
    FuncVec callers(const llvm::Function *F) const { return _impl->callers(F); };
    ///
    /// Get functions called from the given function
    ///
    FuncVec callees(const llvm::Function *F) const { return _impl->callees(F); };
    ///
    /// Return true if function 'F' calls 'what'
    ///
    bool calls(const llvm::Function *F, const llvm::Function *what) const {
        return _impl->calls(F, what);
    };
};

} // llvmdg
} // dg

#endif
