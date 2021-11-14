#include "dg/llvm/ValueRelations/RelationsAnalyzer.h"

#include <algorithm>

namespace dg {
namespace vr {

using V = ValueRelations::V;

// ********************** points to invalidation ********************** //
void RelationsAnalyzer::addAndUnwrapLoads(
        std::set<std::pair<V, unsigned>> &writtenTo, V val) const {
    unsigned depth = 0;
    writtenTo.emplace(val, 0);
    while (auto load = llvm::dyn_cast<llvm::LoadInst>(val)) {
        writtenTo.emplace(load->getPointerOperand(), ++depth);
        val = load->getPointerOperand();
    }
}

std::set<std::pair<V, unsigned>>
RelationsAnalyzer::instructionInvalidates(I inst) const {
    if (!inst->mayWriteToMemory() && !inst->mayHaveSideEffects())
        return std::set<std::pair<V, unsigned>>();

    if (auto intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(inst)) {
        if (isIgnorableIntrinsic(intrinsic->getIntrinsicID())) {
            return std::set<std::pair<V, unsigned>>();
        }
    }

    if (auto call = llvm::dyn_cast<llvm::CallInst>(inst)) {
        auto function = call->getCalledFunction();
        if (function && safeFunctions.find(function->getName().str()) !=
                                safeFunctions.end())
            return std::set<std::pair<V, unsigned>>();
    }

    std::set<std::pair<V, unsigned>> unsetAll = {{nullptr, 0}};

    auto store = llvm::dyn_cast<llvm::StoreInst>(inst);
    if (!store) // most probably CallInst
        // unable to presume anything about such instruction
        return unsetAll;

    // if store writes to a fix location, it cannot be easily said which
    // values it affects
    if (llvm::isa<llvm::Constant>(store->getPointerOperand()))
        return unsetAll;

    V memoryPtr = store->getPointerOperand();
    V underlyingPtr = stripCastsAndGEPs(memoryPtr);

    std::set<std::pair<V, unsigned>> writtenTo;
    // DANGER TODO unset everything in between too
    addAndUnwrapLoads(writtenTo, underlyingPtr); // unset underlying memory
    addAndUnwrapLoads(writtenTo, memoryPtr);     // unset pointer itself

    const ValueRelations &graph = codeGraph.getVRLocation(store).relations;

    // every pointer with unknown origin is considered having an alias
    if (mayHaveAlias(graph, memoryPtr) || !hasKnownOrigin(graph, memoryPtr)) {
        // if invalidated memory may have an alias, unset all memory whose
        // origin is unknown since it may be the alias
        for (const auto &fromsValues : graph.getAllLoads()) {
            if (!hasKnownOrigin(graph, fromsValues.first[0])) {
                addAndUnwrapLoads(writtenTo, fromsValues.first[0]);
            }
        }
    }

    if (!hasKnownOrigin(graph, memoryPtr)) {
        // if memory does not have a known origin, unset all values which
        // may have an alias, since this memory may be the alias
        for (const auto &fromsValues : graph.getAllLoads()) {
            if (mayHaveAlias(graph, fromsValues.first[0]))
                addAndUnwrapLoads(writtenTo, fromsValues.first[0]);
        }
    }

    return writtenTo;
}

V RelationsAnalyzer::getInvalidatedPointer(const ValueRelations &graph,
                                           V invalid, unsigned depth) const {
    while (depth && invalid) {
        const auto &values = graph.getValsByPtr(invalid);

        if (values.empty()) {
            invalid = nullptr; // invalidated pointer does not load anything in
                               // current graph
        } else {
            invalid = values[0];
            --depth;
        }
    }
    return graph.hasLoad(invalid) ? invalid : nullptr;
}

std::set<V>
RelationsAnalyzer::instructionInvalidatesFromGraph(const ValueRelations &graph,
                                                   I inst) const {
    const auto &indirectlyInvalid = instructionInvalidates(inst);

    // go through all (indireclty) invalidated pointers and add those
    // that occur in current location
    std::set<V> allInvalid;
    for (const auto &pair : indirectlyInvalid) {
        if (!pair.first) {
            // add all loads in graph
            for (auto &fromsValues : graph.getAllLoads())
                allInvalid.emplace(fromsValues.first[0]);
            break;
        }

        auto directlyInvalid =
                getInvalidatedPointer(graph, pair.first, pair.second);
        if (directlyInvalid)
            allInvalid.emplace(directlyInvalid);
    }
    return allInvalid;
}

// ************************ points to helpers ************************* //
bool RelationsAnalyzer::mayHaveAlias(const ValueRelations &graph, V val) const {
    for (auto eqval : graph.getEqual(val))
        if (mayHaveAlias(eqval))
            return true;
    return false;
}

bool RelationsAnalyzer::mayHaveAlias(V val) const {
    // if value is not pointer, we don't care whether there can be other name
    // for same value
    if (!val->getType()->isPointerTy())
        return false;

    for (const llvm::User *user : val->users()) {
        // if value is stored, it can be accessed
        if (llvm::isa<llvm::StoreInst>(user)) {
            if (user->getOperand(0) == val)
                return true;

        } else if (llvm::isa<llvm::CastInst>(user)) {
            if (mayHaveAlias(user))
                return true;

        } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(user)) {
            if (gep->getPointerOperand() == val) {
                if (gep->hasAllZeroIndices())
                    return true;

                // TODO really? remove
                llvm::Type *valType = val->getType();
                llvm::Type *gepType = gep->getPointerOperandType();
                if (gepType->isVectorTy() || valType->isVectorTy())
                    assert(0 &&
                           "i dont know what it is and when does it happen");
                if (gepType->getPrimitiveSizeInBits() <
                    valType->getPrimitiveSizeInBits())
                    return true;
            }

        } else if (auto intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(user)) {
            if (!isIgnorableIntrinsic(intrinsic->getIntrinsicID()) &&
                intrinsic->mayWriteToMemory())
                return true;

        } else if (auto inst = llvm::dyn_cast<llvm::Instruction>(user)) {
            if (inst->mayWriteToMemory())
                return true;
        }
    }
    return false;
}

bool RelationsAnalyzer::isIgnorableIntrinsic(llvm::Intrinsic::ID id) const {
    switch (id) {
    case llvm::Intrinsic::lifetime_start:
    case llvm::Intrinsic::lifetime_end:
    case llvm::Intrinsic::stacksave:
    case llvm::Intrinsic::stackrestore:
    case llvm::Intrinsic::dbg_declare:
    case llvm::Intrinsic::dbg_value:
        return true;
    default:
        return false;
    }
}

V RelationsAnalyzer::stripCastsAndGEPs(V memoryPtr) {
    memoryPtr = memoryPtr->stripPointerCasts();
    while (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(memoryPtr)) {
        memoryPtr = gep->getPointerOperand()->stripPointerCasts();
    }
    return memoryPtr;
}

bool RelationsAnalyzer::hasKnownOrigin(const ValueRelations &graph, V from) {
    for (auto memoryPtr : graph.getEqual(from)) {
        memoryPtr = stripCastsAndGEPs(memoryPtr);
        if (llvm::isa<llvm::AllocaInst>(memoryPtr))
            return true;
    }
    return false;
}

// ************************ operation helpers ************************* //
bool RelationsAnalyzer::solvesSameType(ValueRelations &graph,
                                       const llvm::ConstantInt *c1,
                                       const llvm::ConstantInt *c2,
                                       const llvm::BinaryOperator *op) {
    if (c1 && c2) {
        llvm::APInt result;

        switch (op->getOpcode()) {
        case llvm::Instruction::Add:
            result = c1->getValue() + c2->getValue();
            break;
        case llvm::Instruction::Sub:
            result = c1->getValue() - c2->getValue();
            break;
        case llvm::Instruction::Mul:
            result = c1->getValue() * c2->getValue();
            break;
        default:
            assert(0 && "solvesSameType: shouldn't handle any other operation");
        }
        graph.setEqual(op, llvm::ConstantInt::get(c1->getType(), result));
        return true;
    }

    llvm::Type *i32 = llvm::Type::getInt32Ty(op->getContext());
    const llvm::Constant *one = llvm::ConstantInt::getSigned(i32, 1);
    const llvm::Constant *minusOne = llvm::ConstantInt::getSigned(i32, -1);

    V fst = op->getOperand(0);
    V snd = op->getOperand(1);

    if (!c1 && !c2) {
        switch (op->getOpcode()) {
        case llvm::Instruction::Add:
            if (graph.isLesserEqual(one, fst))
                graph.setLesser(snd, op);
            if (graph.isLesserEqual(one, snd))
                graph.setLesser(fst, op);
            if (graph.isLesserEqual(fst, minusOne))
                graph.setLesser(op, snd);
            if (graph.isLesserEqual(snd, minusOne))
                graph.setLesser(op, fst);
            break;
        case llvm::Instruction::Sub:
            if (graph.isLesserEqual(one, snd))
                graph.setLesser(op, fst);
            if (graph.isLesserEqual(snd, minusOne))
                graph.setLesser(fst, op);
            break;
        default:
            break;
        }
        return true;
    }
    return false;
}

void RelationsAnalyzer::solvesDiffOne(ValueRelations &graph, V param,
                                      const llvm::BinaryOperator *op,
                                      bool getLesser) {
    std::vector<V> sample = getLesser ? graph.getDirectlyLesser(param)
                                      : graph.getDirectlyGreater(param);

    for (V val : sample) {
        assert(graph.are(val, getLesser ? Relations::LT : Relations::GT,
                         param));
    }

    for (V value : sample)
        if (getLesser)
            graph.setLesserEqual(value, op);
        else
            graph.setLesserEqual(op, value);
}

bool RelationsAnalyzer::operandsEqual(
        ValueRelations &graph, I fst, I snd,
        bool sameOrder) const { // false means checking in reverse order
    unsigned total = fst->getNumOperands();
    if (total != snd->getNumOperands())
        return false;

    for (unsigned i = 0; i < total; ++i) {
        unsigned otherI = sameOrder ? i : total - i - 1;

        if (!graph.isEqual(fst->getOperand(i), snd->getOperand(otherI)))
            return false;
    }
    return true;
}

void RelationsAnalyzer::solveByOperands(ValueRelations &graph,
                                        const llvm::BinaryOperator *operation,
                                        bool sameOrder) {
    for (auto same : structure.getInstructionSetFor(operation->getOpcode())) {
        auto sameOperation = llvm::dyn_cast<const llvm::BinaryOperator>(same);

        if (operandsEqual(graph, operation, sameOperation, sameOrder))
            graph.setEqual(operation, sameOperation);
    }
}

void RelationsAnalyzer::solveEquality(ValueRelations &graph,
                                      const llvm::BinaryOperator *operation) {
    solveByOperands(graph, operation, true);
}

void RelationsAnalyzer::solveCommutativity(
        ValueRelations &graph, const llvm::BinaryOperator *operation) {
    solveByOperands(graph, operation, false);
}

// ******************** gen from instruction ************************** //
void RelationsAnalyzer::storeGen(ValueRelations &graph,
                                 const llvm::StoreInst *store) {
    graph.setLoad(store->getPointerOperand()->stripPointerCasts(),
                  store->getValueOperand());
}

void RelationsAnalyzer::loadGen(ValueRelations &graph,
                                const llvm::LoadInst *load) {
    graph.setLoad(load->getPointerOperand()->stripPointerCasts(), load);
}

void RelationsAnalyzer::gepGen(ValueRelations &graph,
                               const llvm::GetElementPtrInst *gep) {
    if (gep->hasAllZeroIndices())
        graph.setEqual(gep, gep->getPointerOperand());

    for (auto &fromsValues : graph.getAllLoads()) {
        for (V from : fromsValues.first) {
            if (auto otherGep = llvm::dyn_cast<llvm::GetElementPtrInst>(from)) {
                if (operandsEqual(graph, gep, otherGep, true))
                    graph.setEqual(gep, otherGep);
            }
        }
    }
    // TODO something more?
    // indices method gives iterator over indices
}

void RelationsAnalyzer::extGen(ValueRelations &graph,
                               const llvm::CastInst *ext) {
    graph.setEqual(ext, ext->getOperand(0));
}

void RelationsAnalyzer::addGen(ValueRelations &graph,
                               const llvm::BinaryOperator *add) {
    auto c1 = llvm::dyn_cast<llvm::ConstantInt>(add->getOperand(0));
    auto c2 = llvm::dyn_cast<llvm::ConstantInt>(add->getOperand(1));
    // TODO check wheter equal to constant

    solveEquality(graph, add);
    solveCommutativity(graph, add);

    if (solvesSameType(graph, c1, c2, add))
        return;

    V param = nullptr;
    if (c2) {
        c1 = c2;
        param = add->getOperand(0);
    } else
        param = add->getOperand(1);

    assert(c1 && add && param);
    // add = param + c1
    if (c1->isZero())
        return graph.setEqual(add, param);

    else if (c1->isNegative()) {
        // c1 < 0  ==>  param + c1 < param
        graph.setLesser(add, param);

        // lesser < param  ==>  lesser <= param + (-1)
        if (c1->isMinusOne())
            solvesDiffOne(graph, param, add, true);

    } else {
        // c1 > 0 => param < param + c1
        graph.setLesser(param, add);

        // param < greater => param + 1 <= greater
        if (c1->isOne())
            solvesDiffOne(graph, param, add, false);
    }

    const llvm::ConstantInt *constBound = graph.getLesserEqualBound(param);
    if (constBound) {
        const llvm::APInt &boundResult =
                constBound->getValue() + c1->getValue();
        const llvm::Constant *llvmResult =
                llvm::ConstantInt::get(add->getType(), boundResult);
        if (graph.isLesser(constBound, param))
            graph.setLesser(llvmResult, add);
        else if (graph.isEqual(constBound, param))
            graph.setEqual(llvmResult, add);
        else
            graph.setLesserEqual(llvmResult, add);
    }
}

void RelationsAnalyzer::subGen(ValueRelations &graph,
                               const llvm::BinaryOperator *sub) {
    auto c1 = llvm::dyn_cast<llvm::ConstantInt>(sub->getOperand(0));
    auto c2 = llvm::dyn_cast<llvm::ConstantInt>(sub->getOperand(1));
    // TODO check wheter equal to constant

    solveEquality(graph, sub);

    if (solvesSameType(graph, c1, c2, sub))
        return;

    if (c1) {
        // TODO collect something here?
        return;
    }

    V param = sub->getOperand(0);
    assert(c2 && sub && param);
    // sub = param - c1
    if (c2->isZero())
        return graph.setEqual(sub, param);

    else if (c2->isNegative()) {
        // c1 < 0  ==>  param < param - c1
        graph.setLesser(param, sub);

        // param < greater ==> param - (-1) <= greater
        if (c2->isMinusOne())
            solvesDiffOne(graph, param, sub, false);

    } else {
        // c1 > 0 => param - c1 < param
        graph.setLesser(sub, param);

        // lesser < param  ==>  lesser <= param - 1
        if (c2->isOne())
            solvesDiffOne(graph, param, sub, true);
    }

    const llvm::ConstantInt *constBound = graph.getLesserEqualBound(param);
    if (constBound) {
        const llvm::APInt &boundResult =
                constBound->getValue() - c2->getValue();
        const llvm::Constant *llvmResult =
                llvm::ConstantInt::get(sub->getType(), boundResult);

        if (graph.isLesser(constBound, param))
            graph.setLesser(llvmResult, sub);
        else if (graph.isEqual(constBound, param))
            graph.setEqual(llvmResult, sub);
        else
            graph.setLesserEqual(llvmResult, sub);
    }
}

void RelationsAnalyzer::mulGen(ValueRelations &graph,
                               const llvm::BinaryOperator *mul) {
    auto c1 = llvm::dyn_cast<llvm::ConstantInt>(mul->getOperand(0));
    auto c2 = llvm::dyn_cast<llvm::ConstantInt>(mul->getOperand(1));
    // TODO check wheter equal to constant

    solveEquality(graph, mul);
    solveCommutativity(graph, mul);

    if (solvesSameType(graph, c1, c2, mul))
        return;

    V param = nullptr;
    if (c2) {
        c1 = c2;
        param = mul->getOperand(0);
    } else
        param = mul->getOperand(1);

    assert(c1 && mul && param);
    // mul = param + c1
    if (c1->isZero())
        return graph.setEqual(mul, c1);
    else if (c1->isOne())
        return graph.setEqual(mul, param);

    // TODO collect something here?
}

void RelationsAnalyzer::remGen(ValueRelations &graph,
                               const llvm::BinaryOperator *rem) {
    assert(rem);
    const llvm::Constant *zero =
            llvm::ConstantInt::getSigned(rem->getType(), 0);

    if (!graph.isLesserEqual(zero, rem->getOperand(0)))
        return;

    graph.setLesserEqual(zero, rem);
    graph.setLesser(rem, rem->getOperand(1));
}

void RelationsAnalyzer::castGen(ValueRelations &graph,
                                const llvm::CastInst *cast) {
    if (cast->isLosslessCast() || cast->isNoopCast(module.getDataLayout()))
        graph.setEqual(cast, cast->getOperand(0));
}

// ******************** process assumption ************************** //
RelationsAnalyzer::Relation
RelationsAnalyzer::ICMPToRel(const llvm::ICmpInst *icmp, bool assumption) {
    llvm::ICmpInst::Predicate pred = assumption ? icmp->getSignedPredicate()
                                                : icmp->getInversePredicate();

    switch (pred) {
    case llvm::ICmpInst::Predicate::ICMP_EQ:
        return Relation::EQ;
    case llvm::ICmpInst::Predicate::ICMP_NE:
        return Relation::NE;
    case llvm::ICmpInst::Predicate::ICMP_ULE:
    case llvm::ICmpInst::Predicate::ICMP_SLE:
        return Relation::LE;
    case llvm::ICmpInst::Predicate::ICMP_ULT:
    case llvm::ICmpInst::Predicate::ICMP_SLT:
        return Relation::LT;
    case llvm::ICmpInst::Predicate::ICMP_UGE:
    case llvm::ICmpInst::Predicate::ICMP_SGE:
        return Relation::GE;
    case llvm::ICmpInst::Predicate::ICMP_UGT:
    case llvm::ICmpInst::Predicate::ICMP_SGT:
        return Relation::GT;
    default:
#ifndef NDEBUG
        llvm::errs() << "Unhandled predicate in" << *icmp << "\n";
#endif
        abort();
    }
}

bool RelationsAnalyzer::processICMP(const ValueRelations &oldGraph,
                                    ValueRelations &newGraph,
                                    VRAssumeBool *assume) const {
    const llvm::ICmpInst *icmp = llvm::cast<llvm::ICmpInst>(assume->getValue());
    bool assumption = assume->getAssumption();

    V op1 = icmp->getOperand(0);
    V op2 = icmp->getOperand(1);

    Relation rel = ICMPToRel(icmp, assumption);

    if (oldGraph.hasConflictingRelation(op1, op2, rel))
        return false;

    newGraph.set(op1, rel, op2);
    return true;
}

bool RelationsAnalyzer::processPhi(ValueRelations &newGraph,
                                   VRAssumeBool *assume) const {
    const llvm::PHINode *phi = llvm::cast<llvm::PHINode>(assume->getValue());
    bool assumption = assume->getAssumption();

    const llvm::BasicBlock *assumedPred = nullptr;
    for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
        V result = phi->getIncomingValue(i);
        auto constResult = llvm::dyn_cast<llvm::ConstantInt>(result);
        if (!constResult ||
            (constResult && ((constResult->isOne() && assumption) ||
                             (constResult->isZero() && !assumption)))) {
            if (!assumedPred)
                assumedPred = phi->getIncomingBlock(i);
            else
                return true; // we found other viable incoming block
        }
    }
    assert(assumedPred);
    assert(assumedPred->size() > 1);
    const llvm::Instruction &lastBeforeTerminator =
            *std::prev(std::prev(assumedPred->end()));

    VRLocation &source = codeGraph.getVRLocation(&lastBeforeTerminator);
    bool result = newGraph.merge(source.relations);
    assert(result);
    return true;
}

// *********************** merge helpers **************************** //
Relations RelationsAnalyzer::relationsInAllPreds(const VRLocation &location,
                                                 V lt, Relations known,
                                                 V rt) const {
    for (VREdge *predEdge : location.predecessors) {
        known &= predEdge->source->relations.between(lt, rt);
        if (!known.any())
            break;
    }
    return known;
}

void RelationsAnalyzer::checkRelatesInAll(VRLocation &location, V lt,
                                          Relations known, V rt,
                                          std::set<V> &setEqual) {
    if (lt == rt) // would add a bucket for every value, even if not related
        return;

    ValueRelations &thisGraph = location.relations;

    Relations related = relationsInAllPreds(location, lt, known, rt);
    if (!related.any())
        return;

    if (related.get() == Relation::EQ)
        setEqual.emplace(rt);
    thisGraph.set(lt, related.get(), rt);
}

bool RelationsAnalyzer::relatesByLoadInAll(
        const std::vector<VRLocation *> &locations, V related, V from,
        Relation rel, bool flip) const {
    for (const VRLocation *vrloc : locations) {
        const std::vector<V> &loaded = vrloc->relations.getValsByPtr(from);
        if (loaded.empty() ||
            (!flip && !vrloc->relations.are(related, rel, loaded[0])) ||
            (flip && !vrloc->relations.are(loaded[0], rel, related)))
            return false;
    }
    return true;
}

bool RelationsAnalyzer::loadsInAll(const std::vector<VRLocation *> &locations,
                                   V from, V value) const {
    for (const VRLocation *vrloc : locations) {
        if (!vrloc->relations.isLoad(from, value))
            // DANGER does it suffice that from equals to value's ptr (before
            // instruction on edge)?
            return false;
    }
    return true;
}

bool RelationsAnalyzer::loadsSomethingInAll(
        const std::vector<VRLocation *> &locations, V from) const {
    for (const VRLocation *vrloc : locations) {
        if (!vrloc->relations.hasLoad(from))
            return false;
    }
    return true;
}

bool RelationsAnalyzer::hasConflictLoad(const std::vector<VRLocation *> &preds,
                                        V from, V val) {
    for (const VRLocation *pred : preds) {
        for (const auto &fromsValues : pred->relations.getAllLoads()) {
            auto findFrom = std::find(fromsValues.first.begin(),
                                      fromsValues.first.end(), from);
            auto findVal = std::find(fromsValues.second.begin(),
                                     fromsValues.second.end(), val);

            if (findFrom != fromsValues.first.end() &&
                findVal == fromsValues.second.end())
                return true;
        }
    }
    return false;
}

bool RelationsAnalyzer::anyInvalidated(const std::set<V> &allInvalid,
                                       const std::vector<V> &froms) {
    for (auto from : froms) {
        if (allInvalid.find(from) != allInvalid.end())
            return true;
    }
    return false;
}

bool RelationsAnalyzer::isGoodFromForPlaceholder(
        const std::vector<VRLocation *> &preds, V from,
        const std::vector<V> values) {
    if (!loadsSomethingInAll(preds, from))
        return false;

    for (auto value : values) {
        if (loadsInAll(preds, from, value))
            return false;
    }
    return true;
}

void RelationsAnalyzer::inferChangeInLoop(ValueRelations &newGraph,
                                          const std::vector<V> &froms,
                                          VRLocation &location) {
    for (V from : froms) {
        const auto &predEdges = location.predecessors;

        VRLocation *outloopPred = predEdges[0]->type == EdgeType::BACK
                                          ? predEdges[1]->source
                                          : predEdges[0]->source;
        VRLocation *inloopPred = predEdges[0]->type == EdgeType::BACK
                                         ? predEdges[0]->source
                                         : predEdges[1]->source;

        std::vector<V> valsInloop = inloopPred->relations.getValsByPtr(from);
        if (valsInloop.empty())
            continue;
        V valInloop = valsInloop[0];

        std::vector<V> allRelated =
                inloopPred->relations.getAllRelated(valInloop);

        // get some value, that is both related to the value loaded from
        // from at the end of the loop and at the same time is loaded
        // from from in given loop
        V firstLoadInLoop = nullptr;
        for (const auto *val : structure.getInloopValues(location)) {
            const ValueRelations &relations =
                    codeGraph.getVRLocation(val).relations;
            auto invalidated = instructionInvalidatesFromGraph(relations, val);
            if (invalidated.find(from) != invalidated.end())
                break;
            if (auto call = llvm::dyn_cast<llvm::CallInst>(val)) {
                auto function = call->getCalledFunction();
                if (function && safeFunctions.find(function->getName().str()) ==
                                        safeFunctions.end())
                    break; // TODO DANGER do properly
            }

            if (std::find(allRelated.begin(), allRelated.end(), val) !=
                allRelated.end()) {
                if (auto load = llvm::dyn_cast<llvm::LoadInst>(val)) {
                    if (load->getPointerOperand() == from) {
                        firstLoadInLoop = load;
                        break;
                    }
                }
            }
        }

        // set all preserved relations
        if (firstLoadInLoop) {
            // get all equal vals from load from outloopPred
            std::vector<V> valsOutloop =
                    outloopPred->relations.getValsByPtr(from);
            if (valsOutloop.empty())
                continue;

            Handle placeholder = newGraph.newPlaceholderBucket(from);

            if (inloopPred->relations.isLesser(firstLoadInLoop, valInloop))
                newGraph.setLesserEqual(valsOutloop[0], placeholder);

            if (inloopPred->relations.isLesser(valInloop, firstLoadInLoop))
                newGraph.setLesserEqual(placeholder, valsOutloop[0]);

            if (newGraph.hasComparativeRelations(placeholder)) {
                newGraph.setLoad(from, placeholder);

                for (V val : valsOutloop) {
                    newGraph.setEqual(valsOutloop[0], val);
                }
            } else {
                newGraph.erasePlaceholderBucket(placeholder);
            }
        }
    }
}

void RelationsAnalyzer::inferFromChangeLocations(ValueRelations &newGraph,
                                                 VRLocation &location) {
    if (location.isJustLoopJoin()) {
        VRLocation &treePred = location.getTreePredecessor();

        for (auto fromsValues : treePred.relations.getAllLoads()) {
            for (V from : fromsValues.first) {
                std::vector<VRLocation *> locationsAfterInvalidating = {
                        &treePred};

                // get all locations which influence value loaded from from
                for (I invalidating : structure.getInloopValues(location)) {
                    const ValueRelations &relations =
                            codeGraph.getVRLocation(invalidating).relations;
                    auto invalidated = instructionInvalidatesFromGraph(
                            relations, invalidating);

                    if (invalidated.find(from) != invalidated.end()) {
                        locationsAfterInvalidating.emplace_back(
                                codeGraph.getVRLocation(invalidating)
                                        .getSuccLocations()[0]);
                    }
                }

                if (!isGoodFromForPlaceholder(locationsAfterInvalidating, from,
                                              fromsValues.second))
                    continue;

                intersectByLoad(locationsAfterInvalidating, from, newGraph);
            }
        }
    }
}

void RelationsAnalyzer::intersectByLoad(const std::vector<VRLocation *> &preds,
                                        V from, ValueRelations &newGraph) {
    auto &loads = preds[0]->relations.getValsByPtr(from);
    if (loads.empty())
        return;

    const llvm::ConstantInt *bound = nullptr;
    for (VRLocation *pred : preds) {
        const ValueRelations &predGraph = pred->relations;

        auto &loads = predGraph.getValsByPtr(from);
        if (loads.empty())
            return;

        const llvm::ConstantInt *value =
                predGraph.getLesserEqualBound(loads[0]);
        if (!value) {
            bound = nullptr;
            break;
        }

        if (!bound || value->getValue().slt(bound->getValue()))
            bound = value;
    }

    Handle placeholder = newGraph.newPlaceholderBucket(from);

    if (bound)
        newGraph.setLesserEqual(bound, placeholder);

    V loaded = preds[0]->relations.getValsByPtr(from)[0];

    for (auto it = preds[0]->relations.begin_all(loaded);
         it != preds[0]->relations.end_all(loaded); ++it) {
        V related = it->first;
        Relation relation = it->second;
        assert(preds[0]->relations.are(related, relation, loaded));

        if (related == loaded)
            continue;

        switch (relation) {
        case Relation::EQ:
            if (relatesByLoadInAll(preds, related, from, Relation::EQ, false))
                newGraph.setEqual(related, placeholder);

            else if (relatesByLoadInAll(preds, related, from, Relation::LE,
                                        false))
                newGraph.setLesserEqual(related, placeholder);

            else if (relatesByLoadInAll(preds, related, from, Relation::LE,
                                        true))
                newGraph.setLesserEqual(placeholder, related);

            break;

        case Relation::LT:
            if (relatesByLoadInAll(preds, related, from, Relation::LT, false))
                newGraph.setLesser(related, placeholder);

            else if (relatesByLoadInAll(preds, related, from, Relation::LE,
                                        false))
                newGraph.setLesserEqual(related, placeholder);

            break;

        case Relation::LE:
            if (relatesByLoadInAll(preds, related, from, Relation::LE, false))
                newGraph.setLesserEqual(related, placeholder);

            break;

        case Relation::GT:
            if (relatesByLoadInAll(preds, related, from, Relation::LT, true))
                newGraph.setLesser(placeholder, related);

            else if (relatesByLoadInAll(preds, related, from, Relation::LE,
                                        true))
                newGraph.setLesserEqual(placeholder, related);

            break;

        case Relation::GE:
            if (relatesByLoadInAll(preds, related, from, Relation::LE, true))
                newGraph.setLesserEqual(placeholder, related);

            break;

        default:
            assert(0 && "other relations do not participate");
        }
    }

    if (newGraph.hasAnyRelation(placeholder))
        newGraph.setLoad(from, placeholder);
    else
        newGraph.erasePlaceholderBucket(placeholder);
}

// **************************** merge ******************************* //
void RelationsAnalyzer::mergeRelations(VRLocation &location) {
    assert(location.predecessors.size() > 1);

    const ValueRelations &predGraph = location.getTreePredecessor().relations;

    std::set<V> setEqual;
    for (const auto &bucketVal : predGraph.getBucketToVals()) {
        for (const auto &related :
             predGraph.getRelated(bucketVal.first, restricted)) {
            for (V lt : bucketVal.second) {
                if (setEqual.find(lt) !=
                    setEqual.end()) // value has already been set equal to other
                    continue;
                for (V rt : predGraph.getEqual(related.first)) {
                    checkRelatesInAll(location, lt, related.second, rt,
                                      setEqual);
                }
            }
        }
    }

    ValueRelations &thisGraph = location.relations;

    // merge relations from tree predecessor only
    if (location.isJustLoopJoin()) {
        bool result = thisGraph.merge(predGraph, comparative);
        assert(result);

        // merge loads from outloop predecessor, that are not invalidated
        // inside the loop
        // TODO remove in favour of mergeRelatedByLoads

        std::set<V> allInvalid;

        for (const auto *inst : structure.getInloopValues(location)) {
            auto invalid = instructionInvalidatesFromGraph(predGraph, inst);
            allInvalid.insert(invalid.begin(), invalid.end());
        }

        for (const auto &fromsValues : predGraph.getAllLoads()) {
            if (!anyInvalidated(allInvalid, fromsValues.first)) {
                for (auto from : fromsValues.first) {
                    for (auto val : fromsValues.second) {
                        thisGraph.setLoad(from, val);
                    }
                }
            }
        }
    }
}

void RelationsAnalyzer::mergeRelationsByLoads(VRLocation &location) {
    mergeRelationsByLoads(location.getPredLocations(), location);
}

void RelationsAnalyzer::mergeRelationsByLoads(
        const std::vector<VRLocation *> &preds, VRLocation &location) {
    ValueRelations &newGraph = location.relations;

    std::vector<V> froms;
    for (auto fromsValues : preds[0]->relations.getAllLoads()) {
        for (auto from : fromsValues.first) {
            if (isGoodFromForPlaceholder(preds, from, fromsValues.second))
                froms.emplace_back(from);
        }
    }

    // infer some invariants in loop
    if (preds.size() == 2 && location.isJustLoopJoin() &&
        preds[0]->relations.holdsAnyRelations() &&
        preds[1]->relations.holdsAnyRelations())
        inferChangeInLoop(newGraph, froms, location);

    inferFromChangeLocations(newGraph, location);
}

// ***************************** edge ******************************* //
void RelationsAnalyzer::processInstruction(ValueRelations &graph, I inst) {
    switch (inst->getOpcode()) {
    case llvm::Instruction::Store:
        return storeGen(graph, llvm::dyn_cast<llvm::StoreInst>(inst));
    case llvm::Instruction::Load:
        return loadGen(graph, llvm::dyn_cast<llvm::LoadInst>(inst));
    case llvm::Instruction::GetElementPtr:
        return gepGen(graph, llvm::cast<llvm::GetElementPtrInst>(inst));
    case llvm::Instruction::ZExt:
    case llvm::Instruction::SExt: // (S)ZExt should not change value
        return extGen(graph, llvm::dyn_cast<llvm::CastInst>(inst));
    case llvm::Instruction::Add:
        return addGen(graph, llvm::dyn_cast<llvm::BinaryOperator>(inst));
    case llvm::Instruction::Sub:
        return subGen(graph, llvm::dyn_cast<llvm::BinaryOperator>(inst));
    case llvm::Instruction::Mul:
        return mulGen(graph, llvm::dyn_cast<llvm::BinaryOperator>(inst));
    case llvm::Instruction::SRem:
    case llvm::Instruction::URem:
        return remGen(graph, llvm::dyn_cast<llvm::BinaryOperator>(inst));
    default:
        if (auto cast = llvm::dyn_cast<llvm::CastInst>(inst)) {
            return castGen(graph, cast);
        }
    }
}

void RelationsAnalyzer::rememberValidated(const ValueRelations &prev,
                                          ValueRelations &graph, I inst) const {
    std::set<V> invalidated = instructionInvalidatesFromGraph(prev, inst);
    for (auto &fromsValues : graph.getAllLoads()) {
        for (V from : fromsValues.first) {
            assert(!fromsValues.second.empty() ||
                   invalidated.find(from) == invalidated.end());
        }
    }

    for (auto &fromsValues : prev.getAllLoads()) {
        bool validPair = true;
        for (V from : fromsValues.first) {
            if (invalidated.find(from) != invalidated.end()) {
                validPair = false;
                break;
            }
        }
        if (!validPair)
            continue;
        for (V from : fromsValues.first) {
            for (V to : fromsValues.second) {
                graph.setLoad(from, to);
            }
        }
    }
}

bool RelationsAnalyzer::processAssumeBool(const ValueRelations &oldGraph,
                                          ValueRelations &newGraph,
                                          VRAssumeBool *assume) const {
    if (llvm::isa<llvm::ICmpInst>(assume->getValue()))
        return processICMP(oldGraph, newGraph, assume);
    if (llvm::isa<llvm::PHINode>(assume->getValue()))
        return processPhi(newGraph, assume);
    return false; // TODO; probably call
}

bool RelationsAnalyzer::processAssumeEqual(const ValueRelations &oldGraph,
                                           ValueRelations &newGraph,
                                           VRAssumeEqual *assume) const {
    V val1 = assume->getValue();
    V val2 = assume->getAssumption();
    if (oldGraph.hasConflictingRelation(val1, val2, Relation::EQ))
        return false;
    newGraph.setEqual(val1, val2);
    return true;
}

// ************************* topmost ******************************* //
void RelationsAnalyzer::processOperation(VRLocation *source, VRLocation *target,
                                         VROp *op) {
    if (!target)
        return;
    assert(source && target && op);

    ValueRelations &newGraph = target->relations;

    if (op->isInstruction()) {
        newGraph.merge(source->relations, comparative);
        I inst = static_cast<VRInstruction *>(op)->getInstruction();
        rememberValidated(source->relations, newGraph, inst);
        processInstruction(newGraph, inst);
    } else if (op->isAssume()) {
        newGraph.merge(source->relations, Relations().pt());
        bool shouldMerge;
        if (op->isAssumeBool())
            shouldMerge = processAssumeBool(source->relations, newGraph,
                                            static_cast<VRAssumeBool *>(op));
        else // isAssumeEqual
            shouldMerge = processAssumeEqual(source->relations, newGraph,
                                             static_cast<VRAssumeEqual *>(op));
        if (shouldMerge)
            newGraph.merge(source->relations, comparative);
    } else { // else op is noop
        newGraph.merge(source->relations, comparative);
        newGraph.merge(source->relations, Relations().pt());
    }
}

bool RelationsAnalyzer::passFunction(const llvm::Function *function,
                                     bool print) {
    bool changed = false;

    for (auto it = codeGraph.bfs_begin(function);
         it != codeGraph.bfs_end(function); ++it) {
        VRLocation &location = *it;
        if (print) {
            std::cerr << "LOCATION " << location.id << std::endl;
            for (VREdge *predEdge : location.predecessors) {
                std::cerr << predEdge->op->toStr() << std::endl;
            }
        }
        if (print && location.id == 9)
            std::cerr << "pred\n"
                      << location.predecessors[0]->source->relations << "\n"
                      << location.predecessors[1]->source->relations
                      << "before\n"
                      << location.relations << "\n";

        if (location.predecessors.size() > 1) {
            mergeRelations(location);
            mergeRelationsByLoads(location);
        } else if (location.predecessors.size() == 1) {
            VREdge *edge = location.predecessors[0];
            processOperation(edge->source, edge->target, edge->op.get());
        } // else no predecessors => nothing to be passed

        bool locationChanged = location.relations.unsetChanged();
        if (print && locationChanged)
            std::cerr << "after\n" << location.relations;
        changed |= locationChanged;
    }
    return changed;
}

unsigned RelationsAnalyzer::analyze(unsigned maxPass) {
    unsigned maxExecutedPass = 0;

    for (auto &function : module) {
        if (function.isDeclaration())
            continue;

        bool changed = true;
        unsigned passNum = 0;
        while (changed && passNum < maxPass) {
            changed = passFunction(&function, false); // passNum+1==maxPass);
            ++passNum;
        }

        maxExecutedPass = std::max(maxExecutedPass, passNum);
    }

    return maxExecutedPass;
}

} // namespace vr
} // namespace dg
