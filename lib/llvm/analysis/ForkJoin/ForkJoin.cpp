#include <cassert>

#include "ForkJoin.h"

namespace dg {

std::vector<const llvm::Value*>
ForkJoinAnalysis::matchJoin(const llvm::Value *joinVal) {
    using namespace llvm;

    if (_PTA->getOptions().isSVF()) {
        errs() << "ForkJoin analysis does not support SVF yet\n";
        abort();
    }

    auto dgPTA = static_cast<DGLLVMPointerAnalysis*>(_PTA);
    std::vector<const llvm::Value *> threads;

    const auto builder = dgPTA->getBuilder();
    const auto joinCall = cast<CallInst>(joinVal);
    const auto joinNode = builder->findJoin(joinCall);
    for (const auto forkNode : joinNode->forks()) {
        const auto llvmcall = forkNode->callInst()->getUserData<llvm::Value>();
        assert(isa<CallInst>(llvmcall));
        threads.push_back(llvmcall);
    }

    return threads;


   //const auto calledVal = joinCall->getCalledValue();
   //if (const auto joinF = dyn_cast<Function>(calledVal)) {
   //    assert(joinF->getName().equals("pthread_join")
   //            && "Invalid function taken as pthread join");

   //} else {
   //    errs() << "Join via function call not implemented yet\n";
   //    abort();
   //}
}

std::vector<const llvm::Value*>
ForkJoinAnalysis::joinFunctions(const llvm::Value *joinVal) {
    using namespace llvm;

    if (_PTA->getOptions().isSVF()) {
        errs() << "ForkJoin analysis does not support SVF yet\n";
        abort();
    }

    auto dgPTA = static_cast<DGLLVMPointerAnalysis*>(_PTA);
    std::vector<const llvm::Value *> threads;

    const auto builder = dgPTA->getBuilder();
    const auto joinCall = cast<CallInst>(joinVal);
    const auto joinNode = builder->findJoin(joinCall);
    for (const auto function : joinNode->functions()) {
        const auto llvmFunction = function->getUserData<llvm::Value>();
        assert(isa<Function>(llvmFunction));
        threads.push_back(llvmFunction);
    }

    return threads;
}

} // namespace dg
