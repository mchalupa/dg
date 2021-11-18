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
    using HandlePtr = ValueRelations::HandlePtr;
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
    static Shift getShift(const llvm::BinaryOperator *op,
                          const VectorSet<V> &froms);
    static Shift getShift(const llvm::GetElementPtrInst *op,
                          const VectorSet<V> &froms);
    static Shift getShift(const llvm::Value *val, const VectorSet<V> &froms);
    Shift getShift(const std::vector<const VRLocation *> &changeLocations,
                   const VectorSet<V> &froms) const;
    bool canShift(const ValueRelations &graph, V param, Relations::Type shift);
    void solveDifferent(ValueRelations &graph, const llvm::BinaryOperator *op);
    void inferFromNEPointers(ValueRelations &newGraph,
                             VRAssumeBool *assume) const;
    bool findEqualBorderBucket(const ValueRelations &relations,
                               const llvm::Value *mBorderV,
                               const llvm::Value *comparedV);

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
    bool processICMP(const ValueRelations &oldGraph, ValueRelations &newGraph,
                     VRAssumeBool *assume);
    bool processPhi(ValueRelations &newGraph, VRAssumeBool *assume);

    // *********************** merge helpers **************************** //
    template <typename X, typename Y>
    static Relations getCommon(const VRLocation &location, const X &lt,
                               Relations known, const Y &rt) {
        for (VREdge *predEdge : location.predecessors) {
            const ValueRelations &predRels = predEdge->source->relations;
            known &= predRels.between(lt, rt);
            if (!known.any())
                return Relations();
        }
        return known;
    }
    static void inferFromPreds(VRLocation &location, Handle lt, Relations known,
                               Handle rt);
    template <typename X>
    static Relations
    getCommonByPointedTo(const VectorSet<V> &froms,
                         const std::vector<const VRLocation *> &changeLocations,
                         const X &val, Relations rels) {
        for (unsigned i = 1; i < changeLocations.size(); ++i) {
            const ValueRelations &graph = changeLocations[i]->relations;
            HandlePtr from = getCorrespondingByContent(graph, froms);
            if (!from)
                return Relations();
            assert(graph.hasLoad(*from));
            Handle loaded = graph.getPointedTo(*from);

            rels &= graph.between(loaded, val);
            if (!rels.any())
                break;
        }
        return rels;
    }
    std::vector<const VRLocation *>
    getBranchChangeLocations(const VRLocation &join,
                             const VectorSet<V> &froms) const;
    std::vector<const VRLocation *>
    getLoopChangeLocations(const VRLocation &join,
                           const VectorSet<V> &froms) const;

    // get target locations of changing instructions
    std::vector<const VRLocation *>
    getChangeLocations(const VRLocation &join, const VectorSet<V> &froms);
    static std::pair<C, Relations> getBoundOnPointedToValue(
            const std::vector<const VRLocation *> &changeLocations,
            const VectorSet<V> &froms, Relation rel);
    std::vector<const llvm::ICmpInst *> getEQICmp(const VRLocation &join);
    void inferFromNonEquality(VRLocation &join, const VectorSet<V> &froms,
                              Shift s, Handle placeholder);
    void
    inferShiftInLoop(const std::vector<const VRLocation *> &changeLocations,
                     const VectorSet<V> &froms, ValueRelations &newGraph,
                     Handle placeholder);
    static void
    relateBounds(const std::vector<const VRLocation *> &changeLocations,
                 const VectorSet<V> &froms, ValueRelations &newGraph,
                 Handle placeholder);
    static void
    relateValues(const std::vector<const VRLocation *> &changeLocations,
                 const VectorSet<V> &froms, ValueRelations &newGraph,
                 Handle placeholder);

    // **************************** merge ******************************* //
    static void mergeRelations(VRLocation &location);
    void mergeRelationsByPointedTo(VRLocation &location);

    // ***************************** edge ******************************* //
    void processInstruction(ValueRelations &graph, I inst);
    void rememberValidated(const ValueRelations &prev, ValueRelations &graph,
                           I inst) const;
    bool processAssumeBool(const ValueRelations &oldGraph,
                           ValueRelations &newGraph, VRAssumeBool *assume);
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

    static std::vector<V> getFroms(const ValueRelations &rels, V val);
    static HandlePtr getHandleFromFroms(const ValueRelations &rels,
                                        const std::vector<V> &froms);
    static HandlePtr getHandleFromFroms(const ValueRelations &toRels,
                                        const ValueRelations &fromRels, V val);

    static HandlePtr getCorrespondingByContent(const ValueRelations &toRels,
                                               const ValueRelations &fromRels,
                                               Handle h);
    static HandlePtr getCorrespondingByContent(const ValueRelations &toRels,
                                               const VectorSet<V> &vals);

    static HandlePtr getCorrespondingByFrom(const ValueRelations &toRels,
                                            const ValueRelations &fromRels,
                                            Handle h);

    static const llvm::AllocaInst *getOrigin(const ValueRelations &rels, V val);
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATION_RELATIONS_ANALYZER_H_
