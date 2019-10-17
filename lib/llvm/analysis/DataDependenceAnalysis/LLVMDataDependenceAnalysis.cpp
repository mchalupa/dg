// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/GlobalVariable.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/analysis/DataDependence/DataDependence.h"

#include "llvm/analysis/ReachingDefinitions/LLVMRDBuilder.h"

namespace dg {
namespace analysis {

LLVMDataDependenceAnalysis::~LLVMDataDependenceAnalysis() {
    delete builder;
}

LLVMRDBuilder *LLVMDataDependenceAnalysis::createBuilder() {
    assert(m && pta);
    if (_options.isSSA()) {
        return new LLVMRDBuilder(m, pta, _options);
    } else {
        return new LLVMRDBuilder(m, pta, _options,
                                 true /* forget locals at return */);
    }
}

DataDependenceAnalysis *LLVMDataDependenceAnalysis::createDDA() {
    assert(builder);

    // let the compiler do copy-ellision
    auto graph = builder->build();
    return new DataDependenceAnalysis(std::move(graph), _options);
}

RWNode *LLVMDataDependenceAnalysis::getNode(const llvm::Value *val) {
    return builder->getNode(val);
}

const RWNode *LLVMDataDependenceAnalysis::getNode(const llvm::Value *val) const {
    return builder->getNode(val);
}

// let the user get the nodes map, so that we can
// map the points-to informatio back to LLVM nodes
const std::unordered_map<const llvm::Value *, RWNode *>&
LLVMDataDependenceAnalysis::getNodesMap() const {
    return builder->getNodesMap();
}

// the value 'use' must be an instruction that reads from memory
std::vector<llvm::Value *>
LLVMDataDependenceAnalysis::getLLVMDefinitions(llvm::Value *use) {

    std::vector<llvm::Value *> defs;

    auto loc = getNode(use);
    if (!loc) {
        llvm::errs() << "[RD] error: no node for: " << *use << "\n";
        return defs;
    }

    if (loc->getUses().empty()) {
        llvm::errs() << "[RD] error: the queried value has empty uses: " << *use << "\n";
        return defs;
    }

    if (!llvm::isa<llvm::LoadInst>(use) && !llvm::isa<llvm::CallInst>(use)) {
        llvm::errs() << "[RD] error: the queried value is not a use: " << *use << "\n";
    }

    auto rdDefs = getDefinitions(loc);
    if (rdDefs.empty()) {
        static std::set<const llvm::Value *> reported;
        if (reported.insert(use).second) {
            llvm::errs() << "[RD] error: no reaching definition for: " << *use << "\n";
        }
    }

    //map the values
    for (RWNode *nd : rdDefs) {
        assert(nd->getType() != RWNodeType::PHI);
        auto llvmvalue = nd->getUserData<llvm::Value>();
        assert(llvmvalue && "RD node has no value");
        defs.push_back(llvmvalue);
    }

    return defs;
}

} // namespace dg
} // namespace analysis

