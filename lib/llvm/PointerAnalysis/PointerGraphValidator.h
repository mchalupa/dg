#ifndef LLVM_DG_POINTER_SUBGRAPH_VALIDATOR_H_
#define LLVM_DG_POINTER_SUBGRAPH_VALIDATOR_H_

#include "dg/PointerAnalysis/PointerGraph.h"
#include "dg/PointerAnalysis/PointerGraphValidator.h"

namespace dg {
namespace pta {

/**
 * Take PointerGraph instance and check whether it is not broken
 */
class LLVMPointerGraphValidator : public PointerGraphValidator {
    bool reportInvalOperands(const PSNode *n,
                             const std::string &user_err) override;

  public:
    LLVMPointerGraphValidator(const PointerGraph *ps,
                              bool no_connectivity = false)
            : PointerGraphValidator(ps, no_connectivity) {}

    ~LLVMPointerGraphValidator() override = default;
};

} // namespace pta
} // namespace dg

#endif
