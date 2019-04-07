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

#include "dg/llvm/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "LLVMRDBuilder.h"

namespace dg {
namespace analysis {
namespace rd {

LLVMReachingDefinitions::~LLVMReachingDefinitions() {
    delete builder;
}

void LLVMReachingDefinitions::initializeSparseRDA() {
    builder = new LLVMRDBuilder(m, pta, _options);
    // let the compiler do copy-ellision
    auto graph = builder->build();

    RDA = std::unique_ptr<ReachingDefinitionsAnalysis>(
                    new SSAReachingDefinitionsAnalysis(std::move(graph)));
}

void LLVMReachingDefinitions::initializeDenseRDA() {
    builder = new LLVMRDBuilder(m, pta, _options,
                                true /* forget locals at return */);
    auto graph = builder->build();

    RDA = std::unique_ptr<ReachingDefinitionsAnalysis>(
                    new ReachingDefinitionsAnalysis(std::move(graph)));
}

RDNode *LLVMReachingDefinitions::getNode(const llvm::Value *val) {
    return builder->getNode(val);
}

const RDNode *LLVMReachingDefinitions::getNode(const llvm::Value *val) const {
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

// the value 'use' must be an instruction that reads from memory
std::vector<llvm::Value *>
LLVMReachingDefinitions::getLLVMReachingDefinitions(llvm::Value *use) {

    std::vector<llvm::Value *> defs;

    auto loc = getMapping(use);
    if (!loc) {
        llvm::errs() << "[RD] error: no mapping for: " << *use << "\n";
        return defs;
    }

    if (loc->getUses().empty()) {
        llvm::errs() << "[RD] error: the queried value has empty uses: " << *use << "\n";
        return defs;
    }

    if (!llvm::isa<llvm::LoadInst>(use) && !llvm::isa<llvm::CallInst>(use)) {
        llvm::errs() << "[RD] error: the queried value is not a use: " << *use << "\n";
    }

    auto rdDefs = getReachingDefinitions(loc);
    if (rdDefs.empty()) {
        static std::set<const llvm::Value *> reported;
        if (reported.insert(use).second) {
            llvm::errs() << "[RD] error: no reaching definition for: " << *use;
        }
        /*
        llvm::GlobalVariable *GV = llvm::dyn_cast<llvm::GlobalVariable>(use);
        if (!GV || !GV->hasInitializer()) {
        } else {
            // this is global variable and the last definition
            // is the initialization
            defs.insert(GV);
        }
        */
    }

    //map the values
    for (RDNode *nd : rdDefs) {
        assert(nd->getType() != rd::RDNodeType::PHI);
        auto llvmvalue = nd->getUserData<llvm::Value>();
        assert(llvmvalue && "RD node has no value");
        defs.push_back(llvmvalue);
    }

    return defs;
}


/*
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
*/

} // namespace rd
} // namespace dg
} // namespace analysis

