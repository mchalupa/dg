#ifndef LLVM_DG_DD_H_
#define LLVM_DG_DD_H_

#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_os_ostream.h>

#include "dg/DataDependence/DataDependence.h"

#include "dg/llvm/DataDependence/LLVMDataDependenceAnalysisOptions.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

namespace dg {
namespace dda {

class LLVMReadWriteGraphBuilder;

class LLVMDataDependenceAnalysis {
    const llvm::Module *m;
    dg::LLVMPointerAnalysis *pta;
    const LLVMDataDependenceAnalysisOptions _options;
    LLVMReadWriteGraphBuilder *builder{nullptr};
    std::unique_ptr<DataDependenceAnalysis> DDA{nullptr};

    LLVMReadWriteGraphBuilder *createBuilder();
    DataDependenceAnalysis *createDDA();

  public:
    LLVMDataDependenceAnalysis(const llvm::Module *m,
                               dg::LLVMPointerAnalysis *pta,
                               LLVMDataDependenceAnalysisOptions opts = {})
            : m(m), pta(pta), _options(std::move(opts)),
              builder(createBuilder()) {}

    ~LLVMDataDependenceAnalysis();

    void buildGraph() {
        assert(builder);
        assert(pta);

        DDA.reset(createDDA());
    }

    void run() {
        if (!DDA) {
            buildGraph();
        }

        assert(DDA);
        DDA->run();
    }

    const LLVMDataDependenceAnalysisOptions &getOptions() const {
        return _options;
    }

    ReadWriteGraph *getGraph() { return DDA->getGraph(); }
    RWNode *getNode(const llvm::Value *val);
    const RWNode *getNode(const llvm::Value *val) const;
    const llvm::Value *getValue(const RWNode *node) const;

    bool isUse(const llvm::Value *val) const {
        const auto *nd = getNode(val);
        return nd && nd->isUse();
    }

    bool isDef(const llvm::Value *val) const {
        const auto *nd = getNode(val);
        return nd && nd->isDef();
    }

    std::vector<RWNode *> getDefinitions(RWNode *where, RWNode *mem,
                                         const Offset &off, const Offset &len) {
        return DDA->getDefinitions(where, mem, off, len);
    }

    std::vector<RWNode *> getDefinitions(RWNode *use) {
        return DDA->getDefinitions(use);
    }

    std::vector<RWNode *> getDefinitions(llvm::Instruction *where,
                                         llvm::Value *mem, const Offset &off,
                                         const Offset &len) {
        auto *whereN = getNode(where);
        assert(whereN);
        auto *memN = getNode(mem);
        assert(memN);
        return DDA->getDefinitions(whereN, memN, off, len);
    }

    std::vector<RWNode *> getDefinitions(llvm::Value *use) {
        auto *node = getNode(use);
        assert(node);
        return getDefinitions(node);
    }

    // return instructions that define the given value
    // (the value must read from memory, e.g. LoadInst)
    std::vector<llvm::Value *> getLLVMDefinitions(llvm::Value *use);
    std::vector<llvm::Value *> getLLVMDefinitions(llvm::Instruction *where,
                                                  llvm::Value *mem,
                                                  const Offset &off,
                                                  const Offset &len);

    DataDependenceAnalysis *getDDA() { return DDA.get(); }
    const DataDependenceAnalysis *getDDA() const { return DDA.get(); }
};

} // namespace dda
} // namespace dg

#endif
