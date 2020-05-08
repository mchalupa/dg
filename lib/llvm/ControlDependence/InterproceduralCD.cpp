#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/CFG.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/util/debug.h"
#include "llvm/ControlDependence/InterproceduralCD.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

using namespace std;

namespace dg {
namespace llvmdg {

std::vector<const llvm::Function *>
LLVMInterprocCD::getCalledFunctions(const llvm::Value *v) {
    if (auto* F = llvm::dyn_cast<llvm::Function>(v)) {
        return {F};
    }

    if (!PTA)
        return {};

    return dg::getCalledFunctions(v, PTA);
}

static inline bool hasNoSuccessors(const llvm::BasicBlock *bb) {
    return succ_begin(bb) == succ_end(bb);
}

void
LLVMInterprocCD::computeFuncInfo(const llvm::Function *fun) {
    using namespace llvm;

    if (fun->isDeclaration() || hasFuncInfo(fun))
        return;

    DBG_SECTION_BEGIN(cda, "Computing no-return points for function " << fun->getName().str());

    auto& info = _funcInfos[fun];
    for (auto& B : *fun) {
        // no successors and does not return to caller
        // -- this is points of no return :)
        if (hasNoSuccessors(&B) &&
            !isa<ReturnInst>(B.getTerminator())) {
            info.noret.insert(B.getTerminator());
        }

        for (auto& I : B) {
            auto *C = dyn_cast<CallInst>(&I);
            if (!C) {
                continue;
            }

            for (auto *calledFun : getCalledFunctions(C->getCalledValue())) {
                if (calledFun->isDeclaration())
                    continue;

                computeFuncInfo(calledFun);
                auto *fi = getFuncInfo(calledFun);
                if (!fi->noret.empty()) {
                    info.noret.insert(C);
                }
            }
        }
    }
    DBG_SECTION_END(cda, "Done computing no-return points for function " << fun->getName().str());
}

} // namespace llvmdg
} // namespace dg
