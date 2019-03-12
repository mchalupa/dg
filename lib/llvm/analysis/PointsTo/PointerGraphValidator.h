#ifndef _LLVM_DG_POINTER_SUBGRAPH_VALIDATOR_H_
#define _LLVM_DG_POINTER_SUBGRAPH_VALIDATOR_H_

#include "dg/analysis/PointsTo/PointerGraph.h"
#include "dg/analysis/PointsTo/PointerGraphValidator.h"

namespace dg {
namespace analysis {
namespace pta {
namespace debug {

/**
 * Take PointerGraph instance and check
 * whether it is not broken
 */
class LLVMPointerGraphValidator : public PointerGraphValidator {
    bool reportInvalOperands(const PSNode *n, const std::string& user_err) override;

public:
    LLVMPointerGraphValidator(const PointerGraph *ps,
                                 bool no_connectivity = false)
    : PointerGraphValidator(ps, no_connectivity) {}

    ~LLVMPointerGraphValidator() = default;
};

} // namespace debug
} // namespace pta
} // namespace analysis
} // namespace dg



#endif // _LLVM_DG_POINTER_SUBGRAPH_VALIDATOR_H_
