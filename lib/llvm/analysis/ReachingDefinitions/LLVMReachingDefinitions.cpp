#include <llvm/IR/GlobalVariable.h>

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

const RDNode *LLVMReachingDefinitions::getMapping(const llvm::Value *val) const {
    return builder->getMapping(val);
}


std::set<llvm::Value *>
LLVMReachingDefinitions::getLLVMReachingDefinitions(llvm::Value *where, llvm::Value *what,
                                                    const Offset offset, const Offset len) {

    std::set<RDNode *> rdDefs;
    std::set<llvm::Value *> defs;

    auto loc = getMapping(where);
    if (!loc) {
        llvm::errs() << "[RD] error: no mapping for: " << *where << "\n";
        return defs;
    }

    auto val = getMapping(what);
    if (!val) {
        llvm::errs() << "[RD] error: no mapping for: " << *what << "\n";
        return defs;
    }

    loc->getReachingDefinitions(val, offset, len, rdDefs);
    if (rdDefs.empty()) {
        llvm::GlobalVariable *GV = llvm::dyn_cast<llvm::GlobalVariable>(what);
        if (!GV || !GV->hasInitializer()) {
            static std::set<const llvm::Value *> reported;
            if (reported.insert(what).second) {
                llvm::errs() << "[RD] error: no reaching definition for: " << *what;
                llvm::errs() << " in: " << *where;
                llvm::errs() << " off: " << *offset << ", len: " << *len << "\n";
            }
        } else {
            // this is global variable and the last definition
            // is the initialization
            defs.insert(GV);
        }
    }

    // Get reaching definitions for UNKNOWN_MEMORY, those can be our definitions.
    loc->getReachingDefinitions(rd::UNKNOWN_MEMORY, Offset::UNKNOWN,
                                Offset::UNKNOWN, rdDefs);

    //map the values
    for (RDNode *nd : rdDefs) {
        auto llvmvalue = nd->getUserData<llvm::Value>();
        assert(llvmvalue);
        defs.insert(llvmvalue);
    }

    return defs;
}



} // namespace rd
} // namespace dg
} // namespace analysis

