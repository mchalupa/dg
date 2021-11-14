#ifndef DG_LLVM_VALUE_RELATION_RELATIONS_ANALYZER_HPP_
#define DG_LLVM_VALUE_RELATION_RELATIONS_ANALYZER_HPP_

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
    const StructureAnalyzer &structure;

    // ********************** points to invalidation ********************** //
    void addAndUnwrapLoads(std::set<std::pair<V, unsigned>> &writtenTo,
                           V val) const;
    std::set<std::pair<V, unsigned>> instructionInvalidates(I inst) const;
    V getInvalidatedPointer(const ValueRelations &graph, V invalid,
                            unsigned depth) const;
    // returns set of values that have a load in given graph and are invalidated
    // by the instruction
    std::set<V> instructionInvalidatesFromGraph(const ValueRelations &graph,
                                                I inst) const;

    // ************************ points to helpers ************************* //
    bool mayHaveAlias(const ValueRelations &graph, V val) const;
    bool mayHaveAlias(V val) const;
    bool isIgnorableIntrinsic(llvm::Intrinsic::ID id) const;
    static V stripCastsAndGEPs(V memoryPtr);
    static bool hasKnownOrigin(const ValueRelations &graph, V from);

    // ************************ operation helpers ************************* //
    bool solvesSameType(ValueRelations &graph, const llvm::ConstantInt *c1,
                        const llvm::ConstantInt *c2,
                        const llvm::BinaryOperator *op);
    void solvesDiffOne(ValueRelations &graph, V param,
                       const llvm::BinaryOperator *op, bool getLesser);
    bool operandsEqual(ValueRelations &graph, I fst, I snd,
                       bool sameOrder) const;
    void solveByOperands(ValueRelations &graph,
                         const llvm::BinaryOperator *operation, bool sameOrder);
    void solveEquality(ValueRelations &graph,
                       const llvm::BinaryOperator *operation);
    void solveCommutativity(ValueRelations &graph,
                            const llvm::BinaryOperator *operation);

    // ******************** gen from instruction ************************** //
    void storeGen(ValueRelations &graph, const llvm::StoreInst *store);
    void loadGen(ValueRelations &graph, const llvm::LoadInst *load);
    void gepGen(ValueRelations &graph, const llvm::GetElementPtrInst *gep);
    void extGen(ValueRelations &graph, const llvm::CastInst *ext);
    void addGen(ValueRelations &graph, const llvm::BinaryOperator *add);
    void subGen(ValueRelations &graph, const llvm::BinaryOperator *sub);
    void mulGen(ValueRelations &graph, const llvm::BinaryOperator *mul);
    void remGen(ValueRelations &graph, const llvm::BinaryOperator *rem);
    void castGen(ValueRelations &graph, const llvm::CastInst *cast);

    // ******************** process assumption ************************** //
    static Relation ICMPToRel(const llvm::ICmpInst *icmp, bool assumption);
    bool processICMP(const ValueRelations &oldGraph, ValueRelations &newGraph,
                     VRAssumeBool *assume) const;
    bool processPhi(ValueRelations &newGraph, VRAssumeBool *assume) const;

    // *********************** merge helpers **************************** //
    Relations relationsInAllPreds(const VRLocation &location, V lt,
                                  Relations known, V rt) const;
    Relations relationsByLoadInAllPreds(const std::vector<VRLocation *> &preds,
                                        V from, V related) const;
    void checkRelatesInAll(VRLocation &location, V lt, Relations known, V rt,
                           std::set<V> &setEqual);
    bool relatesByLoadInAll(const std::vector<VRLocation *> &preds, V related,
                            V from, Relation rel) const;
    bool loadsInAll(const std::vector<VRLocation *> &locations, V from,
                    V value) const;
    bool loadsSomethingInAll(const std::vector<VRLocation *> &locations,
                             V from) const;
    bool hasConflictLoad(const std::vector<VRLocation *> &preds, V from, V val);
    bool anyInvalidated(const std::set<V> &allInvalid,
                        const std::vector<V> &froms);
    bool isGoodFromForPlaceholder(const std::vector<VRLocation *> &preds,
                                  V from, const std::vector<V> values);
    void inferChangeInLoop(ValueRelations &newGraph,
                           const std::vector<V> &froms, VRLocation &location);
    void inferFromChangeLocations(ValueRelations &newGraph,
                                  VRLocation &location);
    void intersectByLoad(const std::vector<VRLocation *> &preds, V from,
                         ValueRelations &newGraph);
    std::pair<C, Relations>
    getBoundOnPointedToValue(const std::vector<VRLocation *> &preds, V from,
                             Relation rel) const;
    void relateBounds(const std::vector<VRLocation *> &preds, V from,
                      ValueRelations &newGraph, Handle placeholder);
    void relateValues(const std::vector<VRLocation *> &preds, V from,
                      ValueRelations &newGraph, Handle placeholder);

    // **************************** merge ******************************* //
    void mergeRelations(VRLocation &location);
    void mergeRelationsByLoads(VRLocation &location);
    void mergeRelationsByLoads(const std::vector<VRLocation *> &preds,
                               VRLocation &location);

    // ***************************** edge ******************************* //
    void processInstruction(ValueRelations &graph, I inst);
    void rememberValidated(const ValueRelations &prev, ValueRelations &graph,
                           I inst) const;
    bool processAssumeBool(const ValueRelations &oldGraph,
                           ValueRelations &newGraph,
                           VRAssumeBool *assume) const;
    bool processAssumeEqual(const ValueRelations &oldGraph,
                            ValueRelations &newGraph,
                            VRAssumeEqual *assume) const;

    // ************************* topmost ******************************* //
    void processOperation(VRLocation *source, VRLocation *target, VROp *op);
    bool passFunction(const llvm::Function *function, bool print);

  public:
    RelationsAnalyzer(const llvm::Module &m, const VRCodeGraph &g,
                      const StructureAnalyzer &sa)
            : module(m), codeGraph(g), structure(sa) {}

    unsigned analyze(unsigned maxPass);
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATION_RELATIONS_ANALYZER_HPP_
