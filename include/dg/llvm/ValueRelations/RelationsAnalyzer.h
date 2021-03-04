#ifndef DG_LLVM_VALUE_RELATION_RELATIONS_ANALYZER_HPP_
#define DG_LLVM_VALUE_RELATION_RELATIONS_ANALYZER_HPP_

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/Value.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/CFG.h>
SILENCE_LLVM_WARNINGS_POP

#include <algorithm>
#include <string>

#include "GraphElements.h"
#include "ValueRelations.h"
#include "StructureAnalyzer.h"

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace dg {
namespace vr {


class RelationsAnalyzer {

    const std::set<std::string> safeFunctions = { "__VERIFIER_nondet_int", "__VERIFIER_nondet_char" };

    const llvm::Module& module;

    // VRLocation corresponding to the state of the program BEFORE executing the instruction
    const std::map<const llvm::Instruction *, VRLocation *>& locationMapping;
    const std::map<const llvm::BasicBlock *, std::unique_ptr<VRBBlock>>& blockMapping;

    // holds information about structural properties of analyzed module
    // like set of instructions executed in loop starging at given location
    // or possibly set of values defined at given location
    const StructureAnalyzer& structure;

    bool processOperation(VRLocation* source, VRLocation* target, VROp* op) {
        if (!target) return false;
        assert(source && target && op);

        ValueRelations newGraph = source->relations;

        if (op->isInstruction()) {
            const llvm::Instruction* inst = static_cast<VRInstruction*>(op)->getInstruction();
            forgetInvalidated(newGraph, inst);
            processInstruction(newGraph, inst);
        } else if (op->isAssume()) { 
            if (op->isAssumeBool())
                processAssumeBool(newGraph, static_cast<VRAssumeBool*>(op));
            else // isAssumeEqual
                processAssumeEqual(newGraph, static_cast<VRAssumeEqual*>(op));
        } // else op is noop

        return andSwapIfChanged(target->relations, newGraph);
    }

    void forgetInvalidated(ValueRelations& graph, const llvm::Instruction* inst) const {

        for (const llvm::Value* invalid : instructionInvalidatesFromGraph(graph, inst))
            graph.unsetAllLoadsByPtr(invalid);
    }

    void addAndUnwrapLoads(
            std::set<std::pair<const llvm::Value*, unsigned>>& writtenTo,
            const llvm::Value* val) const {

        unsigned depth = 0;
        writtenTo.emplace(val, 0);
        while (auto load = llvm::dyn_cast<llvm::LoadInst>(val)) {
            writtenTo.emplace(load->getPointerOperand(), ++depth);
            val = load->getPointerOperand();
        }
    }

    std::set<std::pair<const llvm::Value*, unsigned>> instructionInvalidates(const llvm::Instruction* inst) const {

        if (!inst->mayWriteToMemory() && !inst->mayHaveSideEffects())
            return std::set<std::pair<const llvm::Value*, unsigned>>();

        if (auto intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(inst)) {
            if (isIgnorableIntrinsic(intrinsic->getIntrinsicID())) {
                return std::set<std::pair<const llvm::Value*, unsigned>>();
            }
        }

        if (auto call = llvm::dyn_cast<llvm::CallInst>(inst)) {
            auto function = call->getCalledFunction();
            if (function && safeFunctions.find(function->getName().str()) != safeFunctions.end())
                return std::set<std::pair<const llvm::Value*, unsigned>>();
        }

        std::set<std::pair<const llvm::Value*, unsigned>> unsetAll = { {nullptr, 0} };

        auto store = llvm::dyn_cast<llvm::StoreInst>(inst);
        if (!store) // most probably CallInst
            // unable to presume anything about such instruction
            return unsetAll;

        // if store writes to a fix location, it cannot be easily said which
        // values it affects
        if (llvm::isa<llvm::Constant>(store->getPointerOperand()))
            return unsetAll;

        const llvm::Value* memoryPtr = store->getPointerOperand();
        const llvm::Value* underlyingPtr = stripCastsAndGEPs(memoryPtr);

        std::set<std::pair<const llvm::Value*, unsigned>> writtenTo;
        // DANGER TODO unset everything in between too
        addAndUnwrapLoads(writtenTo, underlyingPtr); // unset underlying memory
        addAndUnwrapLoads(writtenTo, memoryPtr); // unset pointer itself

        const ValueRelations& graph = locationMapping.at(store)->relations;

        // every pointer with unknown origin is considered having an alias
        if (mayHaveAlias(graph, memoryPtr) || !hasKnownOrigin(graph, memoryPtr)) {

            // if invalidated memory may have an alias, unset all memory whose
            // origin is unknown since it may be the alias
            for (const auto& fromsValues : graph.getAllLoads()) {
                if (!hasKnownOrigin(graph, fromsValues.first[0])) {
                    addAndUnwrapLoads(writtenTo, fromsValues.first[0]);
                }
            }
        }

        if (!hasKnownOrigin(graph, memoryPtr)) {

            // if memory does not have a known origin, unset all values which
            // may have an alias, since this memory may be the alias
            for (const auto& fromsValues : graph.getAllLoads()) {
                if (mayHaveAlias(graph, fromsValues.first[0]))
                    addAndUnwrapLoads(writtenTo, fromsValues.first[0]);
            }
        }

        return writtenTo;
    }

    const llvm::Value* getInvalidatedPointer(
            const ValueRelations& graph,
            const llvm::Value* invalid,
            unsigned depth) const {
    
        while (depth && invalid) {
            const auto& values = graph.getValsByPtr(invalid);

            if (values.empty()) {
                invalid = nullptr; // invalidated pointer does not load anything in current graph
            } else {
                invalid = values[0];
                --depth;
            }
        }
        return graph.hasLoad(invalid) ? invalid : nullptr;
    }

    // returns set of values that have a load in given graph and are invalidated
    // by the instruction
    std::set<const llvm::Value*> instructionInvalidatesFromGraph(
            const ValueRelations& graph,
            const llvm::Instruction* inst) const {

        const auto& indirectlyInvalid = instructionInvalidates(inst);

        // go through all (indireclty) invalidated pointers and add those
        // that occur in current location
        std::set<const llvm::Value*> allInvalid;
        for (const auto& pair : indirectlyInvalid) {
            if (!pair.first) {
                // add all loads in graph
                for (auto& fromsValues : graph.getAllLoads())
                    allInvalid.emplace(fromsValues.first[0]);
                break;
            }

            auto directlyInvalid = getInvalidatedPointer(graph, pair.first, pair.second);
            if (directlyInvalid)
                allInvalid.emplace(directlyInvalid);
        }
        return allInvalid;
    }

    bool mayHaveAlias(const ValueRelations& graph, const llvm::Value* val) const {
        for (auto eqval : graph.getEqual(val))
            if (mayHaveAlias(llvm::cast<llvm::User>(eqval)))
                return true;
        return false;
    }

    bool mayHaveAlias(const llvm::User* val) const {
        // if value is not pointer, we don't care whether there can be other name for same value
        if (!val->getType()->isPointerTy())
            return false;

        for (const llvm::User* user : val->users()) {

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
                    llvm::Type* valType = val->getType();
                    llvm::Type* gepType = gep->getPointerOperandType();
                    if (gepType->isVectorTy() || valType->isVectorTy())
                        assert(0 && "i dont know what it is and when does it happen");
                    if (gepType->getPrimitiveSizeInBits() < valType->getPrimitiveSizeInBits())
                        return true;
                }

            } else if (auto intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(user)) {
                if (!isIgnorableIntrinsic(intrinsic->getIntrinsicID()) && intrinsic->mayWriteToMemory())
                    return true;

            } else if (auto inst = llvm::dyn_cast<llvm::Instruction>(user)) {
                if (inst->mayWriteToMemory())
                    return true;
            }
        }
        return false;
    }

    bool isIgnorableIntrinsic(llvm::Intrinsic::ID id) const {
        switch(id) {
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

    static const llvm::Value* stripCastsAndGEPs(const llvm::Value* memoryPtr) {
        memoryPtr = memoryPtr->stripPointerCasts();
        while (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(memoryPtr)) {
            memoryPtr = gep->getPointerOperand()->stripPointerCasts();
        }
        return memoryPtr;
    }

    static bool hasKnownOrigin(const ValueRelations& graph, const llvm::Value* from) {
        for (auto memoryPtr : graph.getEqual(from)) {
            memoryPtr = stripCastsAndGEPs(memoryPtr);
            if (llvm::isa<llvm::AllocaInst>(memoryPtr))
                return true;
        }
        return false;
    }

    void storeGen(ValueRelations& graph, const llvm::StoreInst* store) {
        graph.setLoad(store->getPointerOperand()->stripPointerCasts(), store->getValueOperand());
    }

    void loadGen(ValueRelations& graph, const llvm::LoadInst* load) {
        graph.setLoad(load->getPointerOperand()->stripPointerCasts(), load);
    }

    void gepGen(ValueRelations& graph, const llvm::GetElementPtrInst* gep) {
        if (gep->hasAllZeroIndices())
            graph.setEqual(gep, gep->getPointerOperand());

        for (auto& fromsValues : graph.getAllLoads()) {
            for (const llvm::Value* from : fromsValues.first) {
                if (auto otherGep = llvm::dyn_cast<llvm::GetElementPtrInst>(from)) {
                    if (operandsEqual(graph, gep, otherGep, true))
                        graph.setEqual(gep, otherGep);
                }
            }
        }
        // TODO something more?
        // indices method gives iterator over indices
    }

    void extGen(ValueRelations& graph, const llvm::CastInst* ext) {
        graph.setEqual(ext, ext->getOperand(0));
    }

    void addGen(ValueRelations& graph, const llvm::BinaryOperator* add) {
        auto c1 = llvm::dyn_cast<llvm::ConstantInt>(add->getOperand(0));
        auto c2 = llvm::dyn_cast<llvm::ConstantInt>(add->getOperand(1));
        // TODO check wheter equal to constant

        solveEquality(graph, add);
        solveCommutativity(graph, add);

        if (solvesSameType(graph, c1, c2, add))
            return;

        const llvm::Value* param = nullptr;
        if (c2) {
            c1 = c2;
            param = add->getOperand(0);
        }
        else
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

        const llvm::ConstantInt* constBound = graph.getLesserEqualBound(param);
        if (constBound) {
            const llvm::APInt& boundResult = constBound->getValue() + c1->getValue();
            const llvm::Constant* llvmResult = llvm::ConstantInt::get(add->getType(), boundResult);
            if (graph.isLesser(constBound, param))
                graph.setLesser(llvmResult, add);
            else if (graph.isEqual(constBound, param))
                graph.setEqual(llvmResult, add);
            else
                graph.setLesserEqual(llvmResult, add);
        }
    }

    void subGen(ValueRelations& graph, const llvm::BinaryOperator* sub) {
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

        const llvm::Value* param = sub->getOperand(0);
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

        const llvm::ConstantInt* constBound = graph.getLesserEqualBound(param);
        if (constBound) {
            const llvm::APInt& boundResult = constBound->getValue() - c2->getValue();
            const llvm::Constant* llvmResult = llvm::ConstantInt::get(sub->getType(), boundResult);
            
            if (graph.isLesser(constBound, param))
                graph.setLesser(llvmResult, sub);
            else if (graph.isEqual(constBound, param))
                graph.setEqual(llvmResult, sub);
            else
                graph.setLesserEqual(llvmResult, sub);
        }
    }

    void mulGen(ValueRelations& graph, const llvm::BinaryOperator* mul) {
        auto c1 = llvm::dyn_cast<llvm::ConstantInt>(mul->getOperand(0));
        auto c2 = llvm::dyn_cast<llvm::ConstantInt>(mul->getOperand(1));
        // TODO check wheter equal to constant

        solveEquality(graph, mul);
        solveCommutativity(graph, mul);

        if (solvesSameType(graph, c1, c2, mul))
            return;

        const llvm::Value* param = nullptr;
        if (c2) {
            c1 = c2;
            param = mul->getOperand(0);
        }
        else
            param = mul->getOperand(1);

        assert(c1 && mul && param);
        // mul = param + c1
        if (c1->isZero())
            return graph.setEqual(mul, c1);
        else if (c1->isOne())
            return graph.setEqual(mul, param);

        // TODO collect something here?
    }

    bool solvesSameType(ValueRelations& graph,
                        const llvm::ConstantInt* c1, const llvm::ConstantInt* c2,
                        const llvm::BinaryOperator* op) {
        if (c1 && c2) {
            llvm::APInt result;
            
            switch(op->getOpcode()) {
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

        llvm::Type* i32 = llvm::Type::getInt32Ty(op->getContext());
        const llvm::Constant* one = llvm::ConstantInt::getSigned(i32, 1);
        const llvm::Constant* minusOne = llvm::ConstantInt::getSigned(i32, -1);

        const llvm::Value* fst = op->getOperand(0);
        const llvm::Value* snd = op->getOperand(1);

        if (!c1 && !c2) {
            switch (op->getOpcode()) {
                case llvm::Instruction::Add:
                    if (graph.isLesserEqual(one, fst)) graph.setLesser(snd, op);
                    if (graph.isLesserEqual(one, snd)) graph.setLesser(fst, op);
                    if (graph.isLesserEqual(fst, minusOne)) graph.setLesser(op, snd);
                    if (graph.isLesserEqual(snd, minusOne)) graph.setLesser(op, fst);
                    break;
                case llvm::Instruction::Sub:
                    if (graph.isLesserEqual(one, snd)) graph.setLesser(op, fst);
                    if (graph.isLesserEqual(snd, minusOne)) graph.setLesser(fst, op);
                    break;
                default:
                    break;
            }
            return true;
        }
        return false;
    }

    void solvesDiffOne(ValueRelations& graph,
                       const llvm::Value* param,
                       const llvm::BinaryOperator* op,
                       bool getLesser) {

        std::vector<const llvm::Value*> sample = getLesser ?
                    graph.getDirectlyLesser(param) : graph.getDirectlyGreater(param);

        for (const llvm::Value* value : sample)
            if (getLesser)
                graph.setLesserEqual(value, op);
            else
                graph.setLesserEqual(op, value);
    }

    bool operandsEqual(ValueRelations& graph,
                       const llvm::Instruction* fst,
                       const llvm::Instruction* snd,
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

    void solveByOperands(ValueRelations& graph, const llvm::BinaryOperator* operation, bool sameOrder) {
        for (auto same : structure.getInstructionSetFor(operation->getOpcode())) {
            auto sameOperation = llvm::dyn_cast<const llvm::BinaryOperator>(same);

            if (operandsEqual(graph, operation, sameOperation, sameOrder))
                graph.setEqual(operation, sameOperation);
        }
    }

    void solveEquality(ValueRelations& graph, const llvm::BinaryOperator* operation) {
        solveByOperands(graph, operation, true);
    }

    void solveCommutativity(ValueRelations& graph, const llvm::BinaryOperator* operation) {
        solveByOperands(graph, operation, false);
    }

    void remGen(ValueRelations& graph, const llvm::BinaryOperator* rem) {
        assert(rem);
        const llvm::Constant* zero = llvm::ConstantInt::getSigned(rem->getType(), 0);

        if (!graph.isLesserEqual(zero, rem->getOperand(0)))
            return;

        graph.setLesserEqual(zero, rem);
        graph.setLesser(rem, rem->getOperand(1));
    }

    void castGen(ValueRelations& graph, const llvm::CastInst* cast) {
        if (cast->isLosslessCast() || cast->isNoopCast(module.getDataLayout()))
            graph.setEqual(cast, cast->getOperand(0));
    }

    void processInstruction(ValueRelations& graph, const llvm::Instruction* inst) {
        switch(inst->getOpcode()) {
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

    void processAssumeBool(ValueRelations& newGraph, VRAssumeBool* assume) const {
        if (llvm::isa<llvm::ICmpInst>(assume->getValue()))
            processICMP(newGraph, assume);
        if (llvm::isa<llvm::PHINode>(assume->getValue()))
            processPhi(newGraph, assume);
    }

    void processICMP(ValueRelations& newGraph, VRAssumeBool* assume) const {
        const llvm::ICmpInst* icmp = llvm::cast<llvm::ICmpInst>(assume->getValue());
        bool assumption = assume->getAssumption();

        const llvm::Value* op1 = icmp->getOperand(0);
        const llvm::Value* op2 = icmp->getOperand(1);

        llvm::ICmpInst::Predicate pred = assumption ?
            icmp->getSignedPredicate() : icmp->getInversePredicate();

        switch (pred) {
            case llvm::ICmpInst::Predicate::ICMP_EQ:
                if (!newGraph.hasConflictingRelation(op1, op2, Relation::EQ)) {
                    newGraph.setEqual(op1, op2);
                    return;
                }
                break;

            case llvm::ICmpInst::Predicate::ICMP_NE:
                if (!newGraph.hasConflictingRelation(op1, op2, Relation::NE)) {
                    newGraph.setNonEqual(op1, op2);
                    return;
                }
                break;

            case llvm::ICmpInst::Predicate::ICMP_ULE:
            case llvm::ICmpInst::Predicate::ICMP_SLE:
                if (!newGraph.hasConflictingRelation(op1, op2, Relation::LE)) {
                    newGraph.setLesserEqual(op1, op2);
                    return;
                }
                break;

            case llvm::ICmpInst::Predicate::ICMP_ULT:
            case llvm::ICmpInst::Predicate::ICMP_SLT:
                if (!newGraph.hasConflictingRelation(op1, op2, Relation::LT)) {
                    newGraph.setLesser(op1, op2);
                    return;
                }
                break;

            case llvm::ICmpInst::Predicate::ICMP_UGE:
            case llvm::ICmpInst::Predicate::ICMP_SGE:
                if (!newGraph.hasConflictingRelation(op1, op2, Relation::GE)) {
                    newGraph.setLesserEqual(op2, op1);
                    return;
                }
                break;

            case llvm::ICmpInst::Predicate::ICMP_UGT:
            case llvm::ICmpInst::Predicate::ICMP_SGT:
                if (!newGraph.hasConflictingRelation(op1, op2, Relation::GT)) {
                    newGraph.setLesser(op2, op1);
                    return;
                }
                break;

            default:
        #ifndef NDEBUG
                llvm::errs() << "Unhandled predicate in" << *icmp << "\n";
        #endif
                abort();
        }

        // reachable only if conflicting relation found
        newGraph.unsetComparativeRelations(op1);
        newGraph.unsetComparativeRelations(op2);
    }

    void processPhi(ValueRelations& newGraph, VRAssumeBool* assume) const {
        const llvm::PHINode* phi = llvm::cast<llvm::PHINode>(assume->getValue());
        bool assumption = assume->getAssumption();

        const llvm::BasicBlock* assumedPred = nullptr;
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
            const llvm::Value* result = phi->getIncomingValue(i);
            auto constResult = llvm::dyn_cast<llvm::ConstantInt>(result);
            if (!constResult || (constResult
                    && ((constResult->isOne() && assumption)
                    || (constResult->isZero() && !assumption)))) {
                if (!assumedPred)
                    assumedPred = phi->getIncomingBlock(i);
                else
                    return; // we found other viable incoming block
            }
        }
        assert (assumedPred);

        VRBBlock* vrbblock = blockMapping.at(assumedPred).get();
        VRLocation* source = vrbblock->last();
        bool result = newGraph.merge(source->relations);
        assert(result);
    }

    void processAssumeEqual(ValueRelations& newGraph, VRAssumeEqual* assume) const {
        const llvm::Value* val1 = assume->getValue();
        const llvm::Value* val2 = assume->getAssumption();
        if (!newGraph.hasConflictingRelation(val1, val2, Relation::EQ))
            newGraph.setEqual(val1, val2);
    }

    bool relatesInAll(const std::vector<VRLocation*>& locations,
                      const llvm::Value* fst,
                      const llvm::Value* snd,
                      bool (ValueRelations::*relates)(const llvm::Value*, const llvm::Value*) const) const {
                      // which is function pointer to isEqual, isLesser, or isLesserEqual

        for (const VRLocation* vrloc : locations) {
            if (!(vrloc->relations.*relates)(fst, snd))
                return false;
        }
        return true;
    }

    bool relatesByLoadInAll(const std::vector<VRLocation*>& locations,
                            const llvm::Value* related,
                            const llvm::Value* from,
                            bool (ValueRelations::*relates)(const llvm::Value*, const llvm::Value*) const,
                            bool flip) const {
        
        for (const VRLocation* vrloc : locations) {
            const std::vector<const llvm::Value*>& loaded = vrloc->relations.getValsByPtr(from);
            if (loaded.empty()
                    || (!flip && !(vrloc->relations.*relates)(related, loaded[0]))
                    || ( flip && !(vrloc->relations.*relates)(loaded[0], related)))
                return false;
        }
        return true;
    }

    bool mergeRelations(VRLocation* location) {
        return mergeRelations(location->getPredLocations(), location);
    }

    bool mergeRelations(const std::vector<VRLocation*>& preds, VRLocation* location) {
        if (preds.empty())
            return false;

        ValueRelations newGraph = location->relations;
        ValueRelations& oldGraph = preds[0]->relations;
        std::vector<const llvm::Value*> values = oldGraph.getAllValues();

        // merge from all predecessors
        for (auto valueIt = values.begin(); valueIt != values.end(); ++valueIt) {
            const llvm::Value* val = *valueIt;

            for (auto it = oldGraph.begin_lesserEqual(val);
                      it != oldGraph.end_lesserEqual(val);
                      ++it) {
                const llvm::Value* related; Relation relation;
                std::tie(related, relation) = *it;

                if (related == val)
                    continue;

                switch (relation) {
                    case Relation::EQ:
                        if (relatesInAll(preds, related, val, &ValueRelations::isEqual)) {
                            newGraph.setEqual(related, val);

                            auto found = std::find(values.begin(), values.end(), related);
                            if (found != values.end()) {
                                values.erase(found);
                                valueIt = std::find(values.begin(), values.end(), val);
                            }
                        }
                        break;

                    case Relation::LT:
                        if (relatesInAll(preds, related, val, &ValueRelations::isLesser))
                            newGraph.setLesser(related, val);
                        break;

                    case Relation::LE:
                        if (relatesInAll(preds, related, val, &ValueRelations::isLesserEqual))
                            newGraph.setLesserEqual(related, val);
                        break;

                    default: assert(0 && "going down, not up");
                }
            }
        }

        // merge relations from tree predecessor only
        VRLocation* treePred = getTreePred(location);
        const ValueRelations& treePredGraph = treePred->relations;

        if (location->isJustLoopJoin()) {
            bool result = newGraph.merge(treePredGraph, true);
            assert(result);
        }

        return andSwapIfChanged(location->relations, newGraph);
    }

    bool loadsInAll(const std::vector<VRLocation*>& locations, const llvm::Value* from, const llvm::Value* value) const {
        for (const VRLocation* vrloc : locations) {
            if (!vrloc->relations.isLoad(from, value))
                // DANGER does it suffice that from equals to value's ptr (before instruction on edge)?
                return false;
        }
        return true;
    }

    bool loadsSomethingInAll(const std::vector<VRLocation*>& locations, const llvm::Value* from) const {
        for (const VRLocation* vrloc : locations) {
            if (!vrloc->relations.hasLoad(from))
                return false;
        }
        return true;
    }

    bool mergeLoads(VRLocation* location) {
        return mergeLoads(location->getPredLocations(), location);
    }

    bool mergeLoads(const std::vector<VRLocation*>& preds, VRLocation* location) {
        if (preds.empty())
            return false;

        ValueRelations newGraph = location->relations;
        const auto& loadBucketPairs = preds[0]->relations.getAllLoads();

        // merge loads from all predecessors
        for (const auto& fromsValues : loadBucketPairs) {
            for (const llvm::Value* from : fromsValues.first) {
                for (const llvm::Value* val : fromsValues.second) {
                    if (loadsInAll(preds, from, val))
                        newGraph.setLoad(from, val);
                }
            }
        }

        // merge loads from outloop predecessor, that are not invalidated
        // inside the loop
        if (location->isJustLoopJoin()) {

            const ValueRelations& oldGraph = getTreePred(location)->relations;

            std::set<const llvm::Value*> allInvalid;

            for (const auto* inst : structure.getInloopValues(location)) {
                auto invalid = instructionInvalidatesFromGraph(oldGraph, inst);
                allInvalid.insert(invalid.begin(), invalid.end());
            }

            for (const auto& fromsValues : oldGraph.getAllLoads()) {
                if (!anyInvalidated(allInvalid, fromsValues.first)) {
                    for (auto from : fromsValues.first) {
                        for (auto val : fromsValues.second) {
                            newGraph.setLoad(from, val);
                        }
                    }
                }
            }
        }

        return andSwapIfChanged(location->relations, newGraph);
    }

    VRLocation* getTreePred(VRLocation* location) const {
        VRLocation* treePred = nullptr;
        for (VREdge* predEdge : location->predecessors) {
            if (predEdge->type == EdgeType::TREE)
                treePred = predEdge->source;
        }
        assert(treePred);
        return treePred;
    }

    bool hasConflictLoad(const std::vector<VRLocation*>& preds,
                         const llvm::Value* from,
                         const llvm::Value* val) {
        for (const VRLocation* pred : preds) {
            for (const auto& fromsValues : pred->relations.getAllLoads()) {
                auto findFrom = std::find(fromsValues.first.begin(), fromsValues.first.end(), from);
                auto findVal = std::find(fromsValues.second.begin(), fromsValues.second.end(), val);
                
                if (findFrom != fromsValues.first.end() && findVal == fromsValues.second.end())
                    return true;
            }
        }
        return false;
    }

    bool anyInvalidated(const std::set<const llvm::Value*>& allInvalid,
                        const std::vector<const llvm::Value*>& froms) {
        for (auto from : froms) {
            if (allInvalid.find(from) != allInvalid.end())
                return true;
        }
        return false;
    }

    bool mergeRelationsByLoads(VRLocation* location) {
        return mergeRelationsByLoads(location->getPredLocations(), location);
    }

    bool mergeRelationsByLoads(const std::vector<VRLocation*>& preds, VRLocation* location) {
        ValueRelations newGraph = location->relations;

        std::vector<const llvm::Value*> froms;
        for (auto fromsValues : preds[0]->relations.getAllLoads()) {
            for (auto from : fromsValues.first) {
                if (isGoodFromForPlaceholder(preds, from, fromsValues.second))
                    froms.emplace_back(from);
            }
        }

        // infer some invariants in loop
        if (preds.size() == 2 && location->isJustLoopJoin()
                && preds[0]->relations.holdsAnyRelations()
                && preds[1]->relations.holdsAnyRelations())
            inferChangeInLoop(newGraph, froms, location);

        inferFromChangeLocations(newGraph, location);

        return andSwapIfChanged(location->relations, newGraph);
    }

    void inferChangeInLoop(ValueRelations& newGraph,
                           const std::vector<const llvm::Value*>& froms,
                           VRLocation* location) {
        for (const llvm::Value* from : froms) {
            const auto& predEdges = location->predecessors;

            VRLocation* outloopPred = predEdges[0]->type == EdgeType::BACK ?
                                        predEdges[1]->source : predEdges[0]->source;
            VRLocation* inloopPred = predEdges[0]->type == EdgeType::BACK ?
                                        predEdges[0]->source : predEdges[1]->source;

            std::vector<const llvm::Value*> valsInloop
                = inloopPred->relations.getValsByPtr(from);
            if (valsInloop.empty())
                continue;
            const llvm::Value* valInloop = valsInloop[0];

            std::vector<const llvm::Value*> allRelated
                = inloopPred->relations.getAllRelated(valInloop);

            // get some value, that is both related to the value loaded from
            // from at the end of the loop and at the same time is loaded
            // from from in given loop
            const llvm::Value* firstLoadInLoop = nullptr;
            for (const auto* val : structure.getInloopValues(location)) {

                const ValueRelations& relations = locationMapping.at(val)->relations;
                auto invalidated = instructionInvalidatesFromGraph(relations, val);
                if (invalidated.find(from) != invalidated.end())
                    break;

                if (std::find(allRelated.begin(), allRelated.end(), val)
                        != allRelated.end()) {
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
                std::vector<const llvm::Value*> valsOutloop
                    = outloopPred->relations.getValsByPtr(from);
                if (valsOutloop.empty())
                    continue;

                unsigned placeholder = newGraph.newPlaceholderBucket();

                if (inloopPred->relations.isLesser(firstLoadInLoop, valInloop))
                    newGraph.setLesserEqual(valsOutloop[0], placeholder);
                    
                if (inloopPred->relations.isLesser(valInloop, firstLoadInLoop))
                    newGraph.setLesserEqual(placeholder, valsOutloop[0]);

                if (newGraph.hasComparativeRelations(placeholder)) {
                    newGraph.setLoad(from, placeholder);

                    for (const llvm::Value* val : valsOutloop) {
                        newGraph.setEqual(valsOutloop[0], val);
                    }
                } else {
                    newGraph.erasePlaceholderBucket(placeholder);
                }
            }
        }
    }

    void inferFromChangeLocations(ValueRelations& newGraph, VRLocation* location) {
        if (location->isJustLoopJoin()) {
            VRLocation* treePred = getTreePred(location);

            for (auto fromsValues : treePred->relations.getAllLoads()) {
                for (const llvm::Value* from : fromsValues.first) {
                    std::vector<VRLocation*> locationsAfterInvalidating = { treePred };

                    // get all locations which influence value loaded from from
                    for (const llvm::Instruction* invalidating : structure.getInloopValues(location)) {
                        const ValueRelations& relations = locationMapping.at(invalidating)->relations;
                        auto invalidated = instructionInvalidatesFromGraph(relations, invalidating);

                        if (invalidated.find(from) != invalidated.end()) {
                            locationsAfterInvalidating.emplace_back(locationMapping.at(invalidating)->getSuccLocations()[0]);
                        }
                    }

                    if (!isGoodFromForPlaceholder(locationsAfterInvalidating, from, fromsValues.second))
                        continue;

                    intersectByLoad(locationsAfterInvalidating, from, newGraph);
                }
            }
        }
    }

    bool isGoodFromForPlaceholder(const std::vector<VRLocation*>& preds, const llvm::Value* from, const std::vector<const llvm::Value*> values) {
         if (!loadsSomethingInAll(preds, from))
            return false;

        for (auto value : values) {
            if (loadsInAll(preds, from, value))
                return false;
        }
        return true;
    }

    void intersectByLoad(const std::vector<VRLocation*>& preds, const llvm::Value* from, ValueRelations& newGraph) {
        auto& loads = preds[0]->relations.getValsByPtr(from);
        if (loads.empty())
            return;

        const llvm::ConstantInt* bound = nullptr;
        for (VRLocation* pred : preds) {
            const ValueRelations& predGraph = pred->relations;

            auto& loads = predGraph.getValsByPtr(from);
            if (loads.empty())
                return;
            
            const llvm::ConstantInt* value = predGraph.getLesserEqualBound(loads[0]);
            if (!value) {
                bound = nullptr;
                break;
            }

            if (!bound || value->getValue().slt(bound->getValue()))
                bound = value;
        }

        unsigned placeholder = newGraph.newPlaceholderBucket();

        if (bound)
            newGraph.setLesserEqual(bound, placeholder);

        const llvm::Value* loaded = preds[0]->relations.getValsByPtr(from)[0];

        for (auto it = preds[0]->relations.begin_all(loaded);
                  it != preds[0]->relations.end_all(loaded);
                ++it) {
            const llvm::Value* related; Relation relation;
            std::tie(related, relation) = *it;

            if (related == loaded)
                continue;

            switch (relation) {
                case Relation::EQ:
                    if (relatesByLoadInAll(preds, related, from, &ValueRelations::isEqual, false))
                        newGraph.setEqual(related, placeholder);
                    
                    else if (relatesByLoadInAll(preds, related, from, &ValueRelations::isLesserEqual, false))
                        newGraph.setLesserEqual(related, placeholder);

                    else if (relatesByLoadInAll(preds, related, from, &ValueRelations::isLesserEqual, true))
                        newGraph.setLesserEqual(placeholder, related);

                    break;

                case Relation::LT:
                    if (relatesByLoadInAll(preds, related, from, &ValueRelations::isLesser, false))
                        newGraph.setLesser(related, placeholder);
                    
                    else if (relatesByLoadInAll(preds, related, from, &ValueRelations::isLesserEqual, false))
                        newGraph.setLesserEqual(related, placeholder);

                    break;
                
                case Relation::LE:
                    if (relatesByLoadInAll(preds, related, from, &ValueRelations::isLesserEqual, false))
                        newGraph.setLesserEqual(related, placeholder);

                    break;
                
                case Relation::GT:
                    if (relatesByLoadInAll(preds, related, from, &ValueRelations::isLesser, true))
                        newGraph.setLesser(placeholder, related);

                    else if (relatesByLoadInAll(preds, related, from, &ValueRelations::isLesserEqual, true))
                        newGraph.setLesserEqual(placeholder, related);

                    break;

                case Relation::GE:
                    if (relatesByLoadInAll(preds, related, from, &ValueRelations::isLesserEqual, true))
                        newGraph.setLesserEqual(placeholder, related);

                    break;

                default:
                    assert(0 && "other relations do not participate");
            }
        }

        if (newGraph.hasComparativeRelations(placeholder))
            newGraph.setLoad(from, placeholder);
        else
            newGraph.erasePlaceholderBucket(placeholder);
    }

    bool andSwapIfChanged(ValueRelations& oldGraph, ValueRelations& newGraph) {
        if (oldGraph.hasAllRelationsFrom(newGraph) && newGraph.hasAllRelationsFrom(oldGraph))
            return false;
            
        swap(oldGraph, newGraph);
        return true;
    }

    bool analysisPass() {
        bool changed = false;

        for (auto& pair : blockMapping) {
            auto& vrblockPtr = pair.second;

            for (auto& locationPtr : vrblockPtr->locations) {

                if (locationPtr->predecessors.size() > 1) {
                    changed = mergeRelations(locationPtr.get())
                            | mergeLoads(locationPtr.get())
                            | mergeRelationsByLoads(locationPtr.get());
                } else if (locationPtr->predecessors.size() == 1) {
                    VREdge* edge = locationPtr->predecessors[0];
                    changed |= processOperation(edge->source, edge->target, edge->op.get());
                } // else no predecessors => nothing to be passed
            }
        }
        return changed;
    }

public:
    RelationsAnalyzer(const llvm::Module& m,
                  std::map<const llvm::Instruction *, VRLocation *>& locs,
                  std::map<const llvm::BasicBlock *, std::unique_ptr<VRBBlock>>& blcs,
                  const StructureAnalyzer& sa)
                  : module(m), locationMapping(locs), blockMapping(blcs), structure(sa) {}

    void analyze(unsigned maxPass) {

        bool changed = true;
        unsigned passNum = 0;
        while (changed && ++passNum <= maxPass)
            changed = analysisPass();
    }
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATION_RELATIONS_ANALYZER_HPP_
