#ifndef DG_LLVM_VALUE_RELATION_STRUCTURE_ANALYZER_H_
#define DG_LLVM_VALUE_RELATION_STRUCTURE_ANALYZER_H_

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

#include <algorithm>

#include "GraphElements.h"
#include "StructureElements.h"
#include "dg/AnalysisOptions.h"

namespace dg {
namespace vr {

class StructureAnalyzer {
    const llvm::Module &module;
    VRCodeGraph &codeGraph;

    // holds vector of instructions, which are processed on any path back to
    // given location is computed only for locations with more than one
    // predecessor
    std::map<const VRLocation *, std::vector<const llvm::Instruction *>>
            inloopValues;

    // holds vector of values, which are defined at given location
    std::map<VRLocation *, std::set<const llvm::Value *>> defined;

    const std::vector<unsigned> collected = {llvm::Instruction::Add,
                                             llvm::Instruction::Sub,
                                             llvm::Instruction::Mul};
    std::map<unsigned, std::set<const llvm::Instruction *>> instructionSets;

    std::vector<AllocatedArea> allocatedAreas;

    std::map<const llvm::Function *, std::vector<CallRelation>>
            callRelationsMap;

    std::map<const llvm::Function *, std::vector<Precondition>>
            preconditionsMap;
    std::map<const llvm::Function *, std::vector<BorderValue>> borderValues;

    void categorizeEdges();

    void findLoops();

    std::set<VRLocation *> collectBackward(const llvm::Function &f,
                                           VRLocation &from);

    void initializeDefined();

    void collectInstructionSet();

    static bool isValidAllocationCall(const llvm::Value *val);

    void collectAllocatedAreas();

    void setValidAreasFromNoPredecessors(std::vector<bool> &validAreas) const;

    std::pair<unsigned, const AllocatedArea *>
    getEqualArea(const ValueRelations &graph, const llvm::Value *ptr) const;

    void invalidateHeapAllocatedAreas(std::vector<bool> &validAreas) const;

    void setValidAreasByInstruction(VRLocation &location,
                                    std::vector<bool> &validAreas,
                                    VRInstruction *vrinst) const;

    void setValidArea(std::vector<bool> &validAreas, const AllocatedArea *area,
                      unsigned index, bool validateThis) const;

    // if heap allocation call was just checked as successful, mark memory valid
    void setValidAreasByAssumeBool(VRLocation &location,
                                   std::vector<bool> &validAreas,
                                   VRAssumeBool *assume) const;

    void
    setValidAreasFromSinglePredecessor(VRLocation &location,
                                       std::vector<bool> &validAreas) const;

    static bool trueInAll(const std::vector<std::vector<bool>> &validInPreds,
                          unsigned index);

    // in returned vector, false signifies that corresponding area is
    // invalidated by some of the passed instructions
    std::vector<bool> getInvalidatedAreas(
            const std::vector<const llvm::Instruction *> &instructions) const;

    void
    setValidAreasFromMultiplePredecessors(VRLocation &location,
                                          std::vector<bool> &validAreas) const;

    void computeValidAreas() const;

    void initializeCallRelations();

  public:
    StructureAnalyzer(const llvm::Module &m, VRCodeGraph &g)
            : module(m), codeGraph(g){};

    void analyzeBeforeRelationsAnalysis();

    void analyzeAfterRelationsAnalysis();

    bool isDefined(VRLocation *loc, const llvm::Value *val) const;

    std::vector<const VREdge *> possibleSources(const llvm::PHINode *phi,
                                                bool bval) const;
    std::vector<const llvm::ICmpInst *>
    getRelevantConditions(const VRAssumeBool *assume) const;

    // assumes that location is valid loop start (join of tree and back edges)
    const std::vector<const llvm::Instruction *> &
    getInloopValues(const VRLocation &location) const {
        return inloopValues.at(&location);
    }

    const std::set<const llvm::Instruction *> &
    getInstructionSetFor(unsigned opcode) const {
        return instructionSets.at(opcode);
    }

    std::pair<unsigned, const AllocatedArea *>
    getAllocatedAreaFor(const llvm::Value *ptr) const;

    unsigned getNumberOfAllocatedAreas() const { return allocatedAreas.size(); }

    const std::vector<CallRelation> &
    getCallRelationsFor(const llvm::Instruction *inst) const;

    void addPrecondition(const llvm::Function *func, const llvm::Argument *lt,
                         Relations::Type rel, const llvm::Value *rt);

    bool hasPreconditions(const llvm::Function *func) const;

    const std::vector<Precondition> &
    getPreconditionsFor(const llvm::Function *func) const;

    size_t addBorderValue(const llvm::Function *func,
                          const llvm::Argument *from,
                          const llvm::Value *stored);

    bool hasBorderValues(const llvm::Function *func) const;

    const std::vector<BorderValue> &
    getBorderValuesFor(const llvm::Function *func) const;

    BorderValue getBorderValueFor(const llvm::Function *func, size_t id) const;

#ifndef NDEBUG
    void dumpBorderValues(std::ostream &out = std::cerr) const;
#endif
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATION_STRUCTURE_ANALYZER_H_
