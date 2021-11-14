#ifndef DG_LLVM_VALUE_RELATION_RELATIONS_ANALYZER_H_
#define DG_LLVM_VALUE_RELATION_RELATIONS_ANALYZER_H_

#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

#include <vector>

#include "GraphElements.h"
#include "StructureAnalyzer.h"
#include "ValueRelations.h"

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace dg {
namespace vr {

class RelationsAnalyzer {
    using Handle = ValueRelations::Handle;
    using HandleRef = ValueRelations::BRef;
    using Relation = Relations::Type;
    using V = ValueRelations::V;
    using C = ValueRelations::C;
    using I = const llvm::Instruction *;

    const std::set<std::string> safeFunctions = {"__VERIFIER_nondet_int",
                                                 "__VERIFIER_nondet_char"};

    const llvm::Module &module;
    const VRCodeGraph &codeGraph;

    // holds information about structural properties of analyzed module
    // like set of instructions executed in loop starging at given location
    // or possibly set of values defined at given location
    StructureAnalyzer &structure;

    // ********************** points to invalidation ********************** //
    static bool isIgnorableIntrinsic(llvm::Intrinsic::ID id);
    bool isSafe(I inst) const;
    static bool isDangerous(I inst);
    bool mayHaveAlias(const ValueRelations &graph, V val) const;
    bool mayHaveAlias(V val) const;
    static bool hasKnownOrigin(const ValueRelations &graph, V from);
    static bool hasKnownOrigin(V from);
    bool mayOverwrite(I inst, V address) const;

    // ************************ operation helpers ************************* //
    static bool operandsEqual(ValueRelations &graph, I fst, I snd,
                              bool sameOrder);
    void solveByOperands(ValueRelations &graph,
                         const llvm::BinaryOperator *operation, bool sameOrder);
    void solveEquality(ValueRelations &graph,
                       const llvm::BinaryOperator *operation);
    void solveCommutativity(ValueRelations &graph,
                            const llvm::BinaryOperator *operation);
    enum class Shift { INC, DEC, EQ, UNKNOWN };
    static Shift getShift(const llvm::BinaryOperator *op, V from);
    static Shift getShift(const llvm::GetElementPtrInst *op, V from);
    static Shift getShift(const llvm::Value *val, V from);
    Shift getShift(const std::vector<const VRLocation *> &changeLocations,
                   V from) const;
    bool canShift(const ValueRelations &graph, V param, Relations::Type shift);
    void solveDifferent(ValueRelations &graph, const llvm::BinaryOperator *op);
    void inferFromNEPointers(ValueRelations &newGraph,
                             VRAssumeBool *assume) const;

    // ******************** gen from instruction ************************** //
    static void storeGen(ValueRelations &graph, const llvm::StoreInst *store);
    static void loadGen(ValueRelations &graph, const llvm::LoadInst *load);
    static void gepGen(ValueRelations &graph,
                       const llvm::GetElementPtrInst *gep);
    static void extGen(ValueRelations &graph, const llvm::CastInst *ext);
    void opGen(ValueRelations &graph, const llvm::BinaryOperator *op);
    static void remGen(ValueRelations &graph, const llvm::BinaryOperator *rem);
    void castGen(ValueRelations &graph, const llvm::CastInst *cast);

    // ******************** process assumption ************************** //
    static Relation ICMPToRel(const llvm::ICmpInst *icmp, bool assumption);
    static bool processICMP(const ValueRelations &oldGraph,
                            ValueRelations &newGraph, VRAssumeBool *assume);
    bool processPhi(const ValueRelations &oldGraph, ValueRelations &newGraph,
                    VRAssumeBool *assume) const;

    // *********************** merge helpers **************************** //
    static Relations getCommon(const VRLocation &location, V lt,
                               Relations known, V rt);
    static void checkRelatesInAll(VRLocation &location, V lt, Relations known,
                                  V rt, std::set<V> &setEqual);
    static Relations
    getCommonByPointedTo(V from,
                         const std::vector<const VRLocation *> &changeRelations,
                         V val, Relations rels);
    std::vector<const VRLocation *>
    getBranchChangeLocations(const VRLocation &join, V from) const;
    std::vector<const VRLocation *>
    getLoopChangeLocations(const VRLocation &join, V form) const;

    // get target locations of changing instructions
    std::vector<const VRLocation *> getChangeLocations(const VRLocation &join,
                                                       V from);
    static std::pair<C, Relations> getBoundOnPointedToValue(
            const std::vector<const VRLocation *> &changeLocations, V from,
            Relation rel);
    std::vector<const llvm::ICmpInst *> getEQICmp(const VRLocation &join);
    void inferFromNonEquality(VRLocation &join, V from,
                              const VectorSet<V> &initial, Shift s,
                              Handle placeholder);
    void
    inferShiftInLoop(const std::vector<const VRLocation *> &changeLocations,
                     V from, ValueRelations &newGraph, Handle placeholder);
    static void
    relateBounds(const std::vector<const VRLocation *> &changeLocations, V from,
                 ValueRelations &newGraph, Handle placeholder);
    static void
    relateValues(const std::vector<const VRLocation *> &changeLocations, V from,
                 ValueRelations &newGraph, Handle placeholder);

    // **************************** merge ******************************* //
    static void mergeRelations(VRLocation &location);
    void mergeRelationsByPointedTo(VRLocation &location);

    // ***************************** edge ******************************* //
    void processInstruction(ValueRelations &graph, I inst);
    void rememberValidated(const ValueRelations &prev, ValueRelations &graph,
                           I inst) const;
    bool processAssumeBool(const ValueRelations &oldGraph,
                           ValueRelations &newGraph,
                           VRAssumeBool *assume) const;
    static bool processAssumeEqual(const ValueRelations &oldGraph,
                                   ValueRelations &newGraph,
                                   VRAssumeEqual *assume);

    // ************************* topmost ******************************* //
    void processOperation(VRLocation *source, VRLocation *target, VROp *op);
    bool passFunction(const llvm::Function &function, bool print);

  public:
    RelationsAnalyzer(const llvm::Module &m, const VRCodeGraph &g,
                      StructureAnalyzer &sa)
            : module(m), codeGraph(g), structure(sa) {}

    unsigned analyze(unsigned maxPass);
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATION_RELATIONS_ANALYZER_H_
