// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "llvm/analysis/PointsTo/PointerGraphValidator.h"

namespace dg {
namespace pta {

static const llvm::Value *getValue(const PSNode *nd) {
    return nd->getUserData<llvm::Value>();
}

bool LLVMPointerGraphValidator::reportInvalOperands(const PSNode *nd, const std::string& user_err) {
    // just check whether the PHI is a pointer type. If it is a number,
    // we do not know whether it is an error.
    if (nd->getType() == PSNodeType::PHI) {
        const llvm::Value *val = getValue(nd);
        assert(val);

        if (val->getType()->isPointerTy()) {
            // this is the PHI node that corresponds to argv, we're fine here
            if (isa<llvm::Argument>(val) && nd->getParent() &&
                nd->getParent()->root->getParent() == nullptr)
                return false;

            return PointerGraphValidator::reportInvalOperands(nd, user_err);
        } else // else issue a warning?
            return false;
    }

    return PointerGraphValidator::reportInvalOperands(nd, user_err);
}


} // namespace pta
} // namespace dg

