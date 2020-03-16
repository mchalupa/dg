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

#include "dg/llvm/DataDependence/DataDependence.h"
#include "llvm/ReadWriteGraph/LLVMReadWriteGraphBuilder.h"

namespace dg {
namespace dda {

LLVMDataDependenceAnalysis::~LLVMDataDependenceAnalysis() {
    delete builder;
}

LLVMReadWriteGraphBuilder *LLVMDataDependenceAnalysis::createBuilder() {
    assert(m && pta);
    return new LLVMReadWriteGraphBuilder(m, pta, _options);
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

const llvm::Value *LLVMDataDependenceAnalysis::getValue(const RWNode *node) const {
    return builder->getValue(node);
}

// let the user get the nodes map, so that we can
// map the points-to informatio back to LLVM nodes
const std::unordered_map<const llvm::Value *, RWNode *>&
LLVMDataDependenceAnalysis::getNodesMapping() const {
    return builder->getNodesMapping();
}

std::vector<llvm::Value *>
LLVMDataDependenceAnalysis::getLLVMDefinitions(llvm::Instruction *where,
                                               llvm::Value *mem,
                                               const Offset& off,
                                               const Offset& len) {

    std::vector<llvm::Value *> defs;

    auto whereN = getNode(where);
    if (!whereN) {
        llvm::errs() << "[RD] error: no node for: " << *where << "\n";
        return defs;
    }

    auto memN = getNode(mem);
    if (!memN) {
        llvm::errs() << "[RD] error: no node for: " << *mem << "\n";
        return defs;
    }

    auto rdDefs = getDefinitions(whereN, memN, off, len);
    if (rdDefs.empty()) {
        static std::set<std::pair<const llvm::Value *, const llvm::Value *>> reported;
        if (reported.insert({where, mem}).second) {
            llvm::errs() << "[RD] error: no reaching definition for: "
                         << *mem << "at " << *where << "\n";
        }
    }

    // map the values
    for (RWNode *nd : rdDefs) {
        assert(nd->getType() != RWNodeType::PHI);
        auto llvmvalue = nd->getUserData<llvm::Value>();
        assert(llvmvalue && "RD node has no value");
        defs.push_back(llvmvalue);
    }

    return defs;
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

} // namespace dda
} // namespace dg

