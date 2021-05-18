#ifndef DG_LLVM_INTERPROC_CD_H_
#define DG_LLVM_INTERPROC_CD_H_

#include <llvm/IR/Module.h>

#include "dg/llvm/ControlDependence/LLVMControlDependenceAnalysisImpl.h"

#include <map>
#include <set>
#include <unordered_map>

namespace llvm {
class Function;
}

namespace dg {

class LLVMPointerAnalysis;

namespace llvmdg {

class CallGraph;

class LLVMInterprocCD : public LLVMControlDependenceAnalysisImpl {
    LLVMPointerAnalysis *PTA{nullptr};
    // CallGraph *_cg{nullptr};

    struct FuncInfo {
        // points that may abort the program
        // (or cause infinite looping). That is,
        // points due to which the function may not return
        // to its caller
        std::set<const llvm::Value *> noret;
        bool hasCD = false;
    };

    std::unordered_map<const llvm::Instruction *, std::set<llvm::Value *>>
            _instrCD;
    std::unordered_map<const llvm::BasicBlock *, std::set<llvm::Value *>>
            _blockCD;
    std::unordered_map<const llvm::Function *, FuncInfo> _funcInfos;

    FuncInfo *getFuncInfo(const llvm::Function *F) {
        auto it = _funcInfos.find(F);
        return it == _funcInfos.end() ? nullptr : &it->second;
    }

    const FuncInfo *getFuncInfo(const llvm::Function *F) const {
        auto it = _funcInfos.find(F);
        return it == _funcInfos.end() ? nullptr : &it->second;
    }

    bool hasFuncInfo(const llvm::Function *fun) const {
        return _funcInfos.find(fun) != _funcInfos.end();
    }

    // recursively compute function info, 'stack' is there to detect recursive
    // calls
    void computeFuncInfo(const llvm::Function *fun,
                         std::set<const llvm::Function *> stack = {});
    void computeCD(const llvm::Function *fun);

    std::vector<const llvm::Function *>
    getCalledFunctions(const llvm::Value *v);

  public:
    using ValVec = LLVMControlDependenceAnalysisImpl::ValVec;

    LLVMInterprocCD(const llvm::Module *module,
                    const LLVMControlDependenceAnalysisOptions &opts = {},
                    LLVMPointerAnalysis *pta = nullptr,
                    CallGraph * /* cg */ = nullptr)
            : LLVMControlDependenceAnalysisImpl(module, opts), PTA(pta)
    /*, _cg(cg) */ {}

    ValVec getNoReturns(const llvm::Function *fun) override {
        ValVec ret;
        auto *fi = getFuncInfo(fun);
        if (!fi) {
            computeFuncInfo(fun);
        }
        fi = getFuncInfo(fun);
        assert(fi && "BUG in computeFuncInfo");

        for (const auto *val : fi->noret)
            ret.push_back(const_cast<llvm::Value *>(val));
        return ret;
    }

    /// Getters of dependencies for a value
    ValVec getDependencies(const llvm::Instruction *I) override {
        const auto *fun = I->getParent()->getParent();
        auto *fi = getFuncInfo(fun);
        if (!fi) {
            computeFuncInfo(fun);
        }

        fi = getFuncInfo(fun);
        assert(fi && "BUG in computeFuncInfo");
        if (!fi->hasCD) {
            computeCD(fun);
            assert(fi->hasCD && "BUG in computeCD");
        }

        ValVec ret;
        auto instrIt = _instrCD.find(I);
        if (instrIt != _instrCD.end()) {
            ret.insert(ret.end(), instrIt->second.begin(),
                       instrIt->second.end());
        }

        auto blkIt = _blockCD.find(I->getParent());
        if (blkIt != _blockCD.end()) {
            ret.insert(ret.end(), blkIt->second.begin(), blkIt->second.end());
        }

        return ret;
    }

    ValVec getDependent(const llvm::Instruction * /*unused*/) override {
        return {};
    }

    /// Getters of dependencies for a basic block
    ValVec getDependencies(const llvm::BasicBlock * /*unused*/) override {
        return {};
    }
    ValVec getDependent(const llvm::BasicBlock * /*unused*/) override {
        return {};
    }

    void compute(const llvm::Function *F = nullptr) override {
        if (F && !F->isDeclaration()) {
            if (!hasFuncInfo(F)) {
                computeFuncInfo(F);
            }
        } else {
            for (const auto &f : *getModule()) {
                if (!f.isDeclaration() && !hasFuncInfo(&f)) {
                    computeFuncInfo(&f);
                }
            }
        }
    }
};

} // namespace llvmdg
} // namespace dg

#endif
