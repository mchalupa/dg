#ifndef DG_LLVM_CALLGRAPH_H_
#define DG_LLVM_CALLGRAPH_H_

#include <vector>
#include <memory>

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
SILENCE_LLVM_WARNINGS_POP

#include "dg/PointerAnalysis/PSNode.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/CallGraph/CallGraph.h"
#include "dg/ADT/Queue.h"
#include "dg/ADT/SetQueue.h"

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

///
/// Callgraph that re-uses the Call graph built during pointer analysis from DG
///
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

///
/// Callgraph that is built based on the results of pointer analysis
///
class LLVMCallGraphImpl : public CallGraphImpl {
    GenericCallGraph<const llvm::Function *> _cg{};
    const llvm::Module *_module;
    LLVMPointerAnalysis *_pta;

    void processBBlock(const llvm::Function *parent,
                       const llvm::BasicBlock& B,
                       ADT::SetQueue<QueueFIFO<const llvm::Function*>>& queue) {
        for (auto& I : B) {
            if (auto *C = llvm::dyn_cast<llvm::CallInst>(&I)) {
#if LLVM_VERSION_MAJOR >= 8
                auto pts = _pta->getLLVMPointsTo(C->getCalledOperand());
#else
                auto pts = _pta->getLLVMPointsTo(C->getCalledValue());
#endif
                for (const auto& ptr : pts) {
                    auto *F = llvm::dyn_cast<llvm::Function>(ptr.value);
                    if (!F)
                        continue;

                    _cg.addCall(parent, F);
                    queue.push(F);
                }
            }
        }
    }

    void build() {
        auto *entry = _module->getFunction(_pta->getOptions().entryFunction);
        assert(entry && "Entry function not found");
        _cg.createNode(entry);

        ADT::SetQueue<QueueFIFO<const llvm::Function*>> queue;
        queue.push(entry);

        while (!queue.empty()) {
            auto *cur = queue.pop();
            for (auto& B : *cur) {
                processBBlock(cur, B, queue);
            }
        }
    }

public:
    LLVMCallGraphImpl(const llvm::Module *m, LLVMPointerAnalysis *pta)
        : _module(m), _pta(pta) {
        build();
    }

    FuncVec functions() const override {
        FuncVec ret;
        for (auto& it : _cg) {
            ret.push_back(it.first);
        }
        return ret;
    }

    FuncVec callers(const llvm::Function *F) const override {
        FuncVec ret;
        auto fnd = _cg.get(F);
        for (auto *nd : fnd->getCallers()) {
            ret.push_back(nd->getValue());
        }
        return ret;
    }

    FuncVec callees(const llvm::Function *F) const override {
        FuncVec ret;
        auto fnd = _cg.get(F);
        for (auto *nd : fnd->getCalls()) {
            ret.push_back(nd->getValue());
        }
        return ret;
    }

    bool calls(const llvm::Function *F, const llvm::Function *what) const override {
        auto fn1 =  _cg.get(F);
        auto fn2 =  _cg.get(what);
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
    CallGraph(const llvm::Module *m, LLVMPointerAnalysis *pta)
        : _impl(new LLVMCallGraphImpl(m, pta)) {}

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
