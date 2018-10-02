#include "dg/analysis/ReachingDefinitions/SemisparseRda.h"
#include "dg/llvm/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "LLVMRDBuilder.h"
#include "LLVMRDBuilderDense.h"
#include "LLVMRDBuilderSemisparse.h"

namespace dg {
namespace analysis {
namespace rd {

LLVMReachingDefinitions::~LLVMReachingDefinitions() {
    delete builder;
}

void LLVMReachingDefinitions::initializeSparseRDA() {
    builder = new LLVMRDBuilderSemisparse(m, pta, _options);
    root = builder->build();

    RDA = std::unique_ptr<ReachingDefinitionsAnalysis>(new SemisparseRda(root));
}

void LLVMReachingDefinitions::initializeDenseRDA() {
    builder = new LLVMRDBuilderDense(m, pta, _options);
    root = builder->build();

    RDA = std::unique_ptr<ReachingDefinitionsAnalysis>(
                    new ReachingDefinitionsAnalysis(root));
}

RDNode *LLVMReachingDefinitions::getNode(const llvm::Value *val) {
    return builder->getNode(val);
}

// let the user get the nodes map, so that we can
// map the points-to informatio back to LLVM nodes
const std::unordered_map<const llvm::Value *, RDNode *>&
LLVMReachingDefinitions::getNodesMap() const {
    return builder->getNodesMap();
}

const std::unordered_map<const llvm::Value *, RDNode *>&
LLVMReachingDefinitions::getMapping() const {
    return builder->getMapping();
}

RDNode *LLVMReachingDefinitions::getMapping(const llvm::Value *val) {
    return builder->getMapping(val);
}



} // namespace rd
} // namespace dg
} // namespace analysis

