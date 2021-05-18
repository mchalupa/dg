#include <cassert>

#include "ForkJoin.h"

namespace dg {

std::vector<const llvm::Value *>
ForkJoinAnalysis::matchJoin(const llvm::Value *joinVal) {
    using namespace llvm;

    if (_PTA->getOptions().isSVF()) {
        errs() << "ForkJoin analysis does not support SVF yet\n";
        abort();
    }

    auto *dgPTA = static_cast<DGLLVMPointerAnalysis *>(_PTA);
    std::vector<const llvm::Value *> threads;

    auto *const builder = dgPTA->getBuilder();
    const auto *const joinCall = cast<CallInst>(joinVal);
    auto *const joinNode = builder->findJoin(joinCall);
    for (auto *const forkNode : joinNode->forks()) {
        auto *const llvmcall = forkNode->callInst()->getUserData<llvm::Value>();
        assert(isa<CallInst>(llvmcall));
        threads.push_back(llvmcall);
    }

    return threads;

    // const auto calledVal = joinCall->getCalledValue();
    // if (const auto joinF = dyn_cast<Function>(calledVal)) {
    //    assert(joinF->getName().equals("pthread_join")
    //            && "Invalid function taken as pthread join");

    //} else {
    //    errs() << "Join via function call not implemented yet\n";
    //    abort();
    //}
}

std::vector<const llvm::Value *>
ForkJoinAnalysis::joinFunctions(const llvm::Value *joinVal) {
    using namespace llvm;

    if (_PTA->getOptions().isSVF()) {
        errs() << "ForkJoin analysis does not support SVF yet\n";
        abort();
    }

    auto *dgPTA = static_cast<DGLLVMPointerAnalysis *>(_PTA);
    std::vector<const llvm::Value *> threads;

    auto *const builder = dgPTA->getBuilder();
    const auto *const joinCall = cast<CallInst>(joinVal);
    auto *const joinNode = builder->findJoin(joinCall);
    for (auto *const function : joinNode->functions()) {
        auto *const llvmFunction = function->getUserData<llvm::Value>();
        assert(isa<Function>(llvmFunction));
        threads.push_back(llvmFunction);
    }

    return threads;
}

} // namespace dg
