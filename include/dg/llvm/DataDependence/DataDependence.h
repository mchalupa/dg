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

class LLVMDataDependenceAnalysis {
    const llvm::Module *m;
    const LLVMDataDependenceAnalysisOptions _options;

  public:
    LLVMDataDependenceAnalysis(const llvm::Module *m,
                               const LLVMDataDependenceAnalysisOptions& opts = {})
            : m(m), _options(std::move(opts)) {}

    virtual ~LLVMDataDependenceAnalysis() {}

    const LLVMDataDependenceAnalysisOptions &getOptions() const {
        return _options;
    }

    const llvm::Module *getModule() const { return m; }

    virtual void run() = 0;

    virtual bool isUse(const llvm::Value *val) const {
        const auto *I = llvm::dyn_cast<llvm::Instruction>(val);
        return I && I->mayReadFromMemory();
    }

    virtual bool isDef(const llvm::Value *val) const {
        const auto *I = llvm::dyn_cast<llvm::Instruction>(val);
        return I && I->mayWriteToMemory();
    }

    // return instructions that define the given value
    // (the value must read from memory, e.g. LoadInst)
    virtual std::vector<llvm::Value *> getLLVMDefinitions(llvm::Value *use) = 0;
    virtual std::vector<llvm::Value *> getLLVMDefinitions(llvm::Instruction *where,
                                                  llvm::Value *mem,
                                                  const Offset &off,
                                                  const Offset &len) = 0;
};

class LLVMReadWriteGraphBuilder;

class DGLLVMDataDependenceAnalysis : public LLVMDataDependenceAnalysis {
    dg::LLVMPointerAnalysis *pta;
    LLVMReadWriteGraphBuilder *builder{nullptr};
    std::unique_ptr<DataDependenceAnalysis> DDA{nullptr};

    LLVMReadWriteGraphBuilder *createBuilder();
    DataDependenceAnalysis *createDDA();

  public:
    DGLLVMDataDependenceAnalysis(const llvm::Module *m,
                                 dg::LLVMPointerAnalysis *pta,
                                 const LLVMDataDependenceAnalysisOptions& opts = {})
            : LLVMDataDependenceAnalysis(m, opts),
              pta(pta), builder(createBuilder()) {}

    ~DGLLVMDataDependenceAnalysis() override;

    void buildGraph() {
        assert(builder);
        assert(pta);

        DDA.reset(createDDA());
    }

    ReadWriteGraph *getGraph() { return DDA->getGraph(); }
    RWNode *getNode(const llvm::Value *val);
    const RWNode *getNode(const llvm::Value *val) const;
    const llvm::Value *getValue(const RWNode *node) const;

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

    DataDependenceAnalysis *getDDA() { return DDA.get(); }
    const DataDependenceAnalysis *getDDA() const { return DDA.get(); }

    void run() override {
        if (!DDA) {
            buildGraph();
        }

        assert(DDA);
        DDA->run();
    }

    bool isUse(const llvm::Value *val) const override {
        const auto *nd = getNode(val);
        return nd && nd->isUse();
    }

    bool isDef(const llvm::Value *val) const override {
        const auto *nd = getNode(val);
        return nd && nd->isDef();
    }

    // return instructions that define the given value
    // (the value must read from memory, e.g. LoadInst)
    std::vector<llvm::Value *> getLLVMDefinitions(llvm::Value *use) override;
    std::vector<llvm::Value *> getLLVMDefinitions(llvm::Instruction *where,
                                                  llvm::Value *mem,
                                                  const Offset &off,
                                                  const Offset &len) override;
};

} // namespace dda
} // namespace dg

#endif
