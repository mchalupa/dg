#include "dg/llvm/ValueRelations/StructureAnalyzer.h"

#include "llvm/llvm-utils.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

#include <algorithm>

#ifndef NDEBUG
#include "dg/llvm/ValueRelations/getValName.h"
#endif

namespace dg {
namespace vr {

const llvm::Value *AllocatedArea::stripCasts(const llvm::Value *inst) {
    while (const auto *cast = llvm::dyn_cast<llvm::CastInst>(inst))
        inst = cast->getOperand(0);
    return inst;
}

uint64_t AllocatedArea::getBytes(const llvm::Type *type) {
    const unsigned byteWidth = 8;
    assert(type->isSized());

    uint64_t size = type->getPrimitiveSizeInBits();

    return size / byteWidth;
}

AllocatedArea::AllocatedArea(const llvm::AllocaInst *alloca) : ptr(alloca) {
    const llvm::Type *allocatedType = alloca->getAllocatedType();

    if (allocatedType->isArrayTy()) {
        const llvm::Type *elemType = allocatedType->getArrayElementType();
        // DANGER just an arbitrary type
        llvm::Type *i32 = llvm::Type::getInt32Ty(elemType->getContext());
        uint64_t intCount = allocatedType->getArrayNumElements();

        originalSizeView = AllocatedSizeView(
                llvm::ConstantInt::get(i32, intCount), getBytes(elemType));
    } else {
        originalSizeView = AllocatedSizeView(alloca->getOperand(0),
                                             getBytes(allocatedType));
    }
}

AllocatedArea::AllocatedArea(const llvm::CallInst *call) : ptr(call) {
    const std::string &name = call->getCalledFunction()->getName().str();
    AnalysisOptions options;

    if (options.getAllocationFunction(name) == AllocationFunction::ALLOCA ||
        options.getAllocationFunction(name) == AllocationFunction::MALLOC) {
        originalSizeView = AllocatedSizeView(call->getOperand(0), 1);
    }

    if (options.getAllocationFunction(name) == AllocationFunction::CALLOC) {
        const auto *size = llvm::cast<llvm::ConstantInt>(call->getOperand(1));
        originalSizeView =
                AllocatedSizeView(call->getOperand(0), size->getZExtValue());
    }

    if (options.getAllocationFunction(name) == AllocationFunction::REALLOC) {
        originalSizeView = AllocatedSizeView(call->getOperand(0), 1);
        reallocatedPtr = call->getOperand(0);
    }
}

std::vector<AllocatedSizeView> AllocatedArea::getAllocatedSizeViews() const {
    std::vector<AllocatedSizeView> result;
    result.emplace_back(originalSizeView);

    AllocatedSizeView currentView = originalSizeView;

    while (const auto *op = llvm::dyn_cast<llvm::BinaryOperator>(
                   stripCasts(currentView.elementCount))) {
        uint64_t size = currentView.elementSize;

        if (op->getOpcode() != llvm::Instruction::Add &&
            op->getOpcode() != llvm::Instruction::Mul)
            // TODO could also handle subtraction of negative constant
            break;

        const auto *c1 = llvm::dyn_cast<llvm::ConstantInt>(op->getOperand(0));
        const auto *c2 = llvm::dyn_cast<llvm::ConstantInt>(op->getOperand(1));

        if (c1 && c2) {
            uint64_t newCount;
            uint64_t newSize;
            switch (op->getOpcode()) {
            case llvm::Instruction::Add:
                // XXX can these overflow?
                newCount = c1->getValue().getZExtValue() +
                           c2->getValue().getZExtValue();
                result.emplace_back(
                        llvm::ConstantInt::get(c1->getType(), newCount), size);
                break;

            case llvm::Instruction::Mul:
                // XXX can these overflow?
                newSize = c1->getValue().getZExtValue() * size;
                result.emplace_back(c2, newSize);
                newSize = c2->getValue().getZExtValue() * size;
                result.emplace_back(c1, newSize);

                newCount = c1->getValue().getZExtValue() *
                           c2->getValue().getZExtValue();
                result.emplace_back(
                        llvm::ConstantInt::get(c1->getType(), newCount), size);
                break;

            default:
                assert(0 && "unreachable");
            }
            // if we found two-constant operation, non of them can be binary
            // operator to further unwrap
            break;
        }

        // TODO use more info here
        if (!c1 && !c2)
            break;

        // else one of c1, c2 is constant and the other is variable
        const llvm::Value *param = nullptr;
        if (c2) {
            c1 = c2;
            param = op->getOperand(0);
        } else
            param = op->getOperand(1);

        // now c1 is constant and param is variable
        assert(c1 && param);

        switch (op->getOpcode()) {
        case llvm::Instruction::Add:
            result.emplace_back(param, size);
            break;

        case llvm::Instruction::Mul:
            result.emplace_back(param, size * c1->getZExtValue());
            break;

        default:
            assert(0 && "unreachable");
        }
        currentView = result.back();
        // reachable only in this last c1 && param case
    }
    return result;
}

#ifndef NDEBUG
void AllocatedArea::ddump() const {
    std::cerr << "Allocated area:"
              << "\n";
    std::cerr << "    ptr " << debug::getValName(ptr) << "\n";
    std::cerr << "    count "
              << debug::getValName(originalSizeView.elementCount) << "\n";
    std::cerr << "    size " << originalSizeView.elementSize << "\n";
    std::cerr << "\n";
}
#endif

void StructureAnalyzer::categorizeEdges() {
    for (const auto &function : module) {
        if (function.isDeclaration())
            continue;

        for (auto it = codeGraph.dfs_begin(function); it != codeGraph.dfs_end();
             ++it) {
            VRLocation &current = *it;
            VREdge *predEdge = it.getEdge();

            if (predEdge)
                predEdge->type = EdgeType::TREE;

            for (unsigned i = 0; i < current.succsSize(); ++i) {
                VREdge *succEdge = current.getSuccEdge(i);
                VRLocation *successor = succEdge->target;

                if (!successor)
                    succEdge->type = EdgeType::TREE;
                else if (it.wasVisited(successor)) {
                    if (it.onStack(successor))
                        succEdge->type = EdgeType::BACK;
                    else
                        succEdge->type = EdgeType::FORWARD;
                }
            }
        }
    }

    codeGraph.hasCategorizedEdges();
}

void StructureAnalyzer::findLoops() {
    for (const auto &function : module) {
        if (function.isDeclaration())
            continue;

        for (auto it = codeGraph.lazy_dfs_begin(function);
             it != codeGraph.lazy_dfs_end(); ++it) {
            VRLocation &location = *it;

            if (!location.isJustLoopJoin())
                continue;

            auto backwardReach = collectBackward(function, location);

            auto &loop =
                    inloopValues
                            .emplace(&location,
                                     std::vector<const llvm::Instruction *>())
                            .first->second;

            for (auto it = codeGraph.lazy_dfs_begin(function, location);
                 it != codeGraph.lazy_dfs_end(); ++it) {
                VRLocation &source = *it;

                if (backwardReach.find(&source) == backwardReach.end())
                    continue;

                for (size_t i = 0; i < source.succsSize(); ++i) {
                    VREdge *edge = source.getSuccEdge(i);
                    if (!edge->target)
                        continue;

                    if (backwardReach.find(edge->target) !=
                        backwardReach.end()) {
                        if (edge->op->isInstruction()) {
                            auto *op = static_cast<VRInstruction *>(
                                    edge->op.get());
                            loop.emplace_back(op->getInstruction());
                        }
                    } else
                        location.loopEnds.emplace_back(edge);
                }

                if (&source != &location && (!source.join || location.join))
                    source.join = &location;
            }
        }
    }
}

std::set<VRLocation *>
StructureAnalyzer::collectBackward(const llvm::Function &f, VRLocation &from) {
    std::vector<VREdge *> &predEdges = from.predecessors;

    // remove the incoming tree edge, so that backwardReach would
    // really go only backwards
    VREdge *treePred = nullptr;
    for (auto it = predEdges.begin(); it != predEdges.end(); ++it) {
        if ((*it)->type == EdgeType::TREE) {
            treePred = *it;
            predEdges.erase(it);
            break;
        }
    }
    assert(treePred); // every join has to have exactly one tree predecessor

    std::set<VRLocation *> result;
    for (auto it = codeGraph.backward_dfs_begin(f, from);
         it != codeGraph.backward_dfs_end(); ++it) {
        result.emplace(&*it);
    }

    assert(result.find(&from) != result.end());

    // put the tree edge back in
    predEdges.emplace_back(treePred);

    return result;
}

void StructureAnalyzer::initializeDefined() {
    for (const llvm::Function &function : module) {
        if (function.isDeclaration())
            continue;

        std::list<VRLocation *> toVisit = {
                &codeGraph.getEntryLocation(function)};

        // prepare sets of defined values for each location
        for (auto &location : codeGraph) {
            defined.emplace(&location, std::set<const llvm::Value *>());
        }

        while (!toVisit.empty()) {
            VRLocation *current = toVisit.front();
            toVisit.pop_front();

            for (unsigned i = 0; i < current->succsSize(); ++i) {
                VREdge *succEdge = current->getSuccEdge(i);

                // if edge leads to nowhere, just continue
                VRLocation *succLoc = succEdge->target;
                if (!succLoc)
                    continue;

                // if edge leads back, we would add values we exactly dont
                // want
                if (succEdge->type == EdgeType::BACK)
                    continue;

                auto &definedSucc = defined.at(succLoc);
                auto &definedHere = defined.at(current);

                // copy from this location to its successor
                definedSucc.insert(definedHere.begin(), definedHere.end());

                // add instruction, if edge carries any
                if (succEdge->op->isInstruction()) {
                    auto *op = static_cast<VRInstruction *>(succEdge->op.get());
                    definedSucc.emplace(op->getInstruction());
                }

                toVisit.push_back(succLoc);
            }
        }
    }
}

void StructureAnalyzer::collectInstructionSet() {
    for (unsigned opcode : collected)
        instructionSets.emplace(opcode, std::set<const llvm::Instruction *>());

    for (const llvm::Function &function : module) {
        for (const llvm::BasicBlock &block : function) {
            for (const llvm::Instruction &inst : block) {
                // if we collect instructions with this opcode
                // add it to its set
                auto found = instructionSets.find(inst.getOpcode());
                if (found != instructionSets.end())
                    found->second.emplace(&inst);
            }
        }
    }
}

bool StructureAnalyzer::isValidAllocationCall(const llvm::Value *val) {
    if (!llvm::isa<llvm::CallInst>(val))
        return false;

    const llvm::CallInst *call = llvm::cast<llvm::CallInst>(val);
    const auto *function = call->getCalledFunction();

    AnalysisOptions options;
    return function &&
           options.isAllocationFunction(function->getName().str()) &&
           (options.getAllocationFunction(function->getName().str()) !=
                    AllocationFunction::CALLOC ||
            llvm::isa<llvm::ConstantInt>(call->getOperand(1)));
}

void StructureAnalyzer::collectAllocatedAreas() {
    // compute allocated areas throughout the code
    for (const llvm::Function &function : module) {
        for (const llvm::BasicBlock &block : function) {
            for (const llvm::Instruction &inst : block) {
                if (const auto *alloca =
                            llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
                    allocatedAreas.emplace_back(alloca);
                }

                else if (const auto *call =
                                 llvm::dyn_cast<llvm::CallInst>(&inst)) {
                    if (isValidAllocationCall(call)) {
                        allocatedAreas.emplace_back(call);
                    }
                }
            }
        }
    }
}

void StructureAnalyzer::setValidAreasFromNoPredecessors(
        std::vector<bool> &validAreas) const {
    validAreas = std::vector<bool>(allocatedAreas.size(), false);
}

std::pair<unsigned, const AllocatedArea *>
StructureAnalyzer::getEqualArea(const ValueRelations &graph,
                                const llvm::Value *ptr) const {
    unsigned index = 0;
    const AllocatedArea *area = nullptr;
    for (const auto *equal : graph.getEqual(ptr)) {
        std::tie(index, area) = getAllocatedAreaFor(equal);
        if (area)
            return {index, area};
    }
    return {0, nullptr};
}

void StructureAnalyzer::invalidateHeapAllocatedAreas(
        std::vector<bool> &validAreas) const {
    unsigned index = 0;
    for (const AllocatedArea &area : allocatedAreas) {
        if (llvm::isa<llvm::CallInst>(area.getPtr()))
            validAreas[index] = false;
        ++index;
    }
}

void StructureAnalyzer::setValidAreasByInstruction(
        VRLocation &location, std::vector<bool> &validAreas,
        VRInstruction *vrinst) const {
    const llvm::Instruction *inst = vrinst->getInstruction();
    unsigned index = 0;
    const AllocatedArea *area = nullptr;

    // every memory allocated on stack is considered allocated successfully
    if (llvm::isa<llvm::AllocaInst>(inst)) {
        std::tie(index, area) = getAllocatedAreaFor(inst);
        assert(area);
        validAreas[index] = true;
    }

    // if came across lifetime_end call, then mark memory whose scope ended
    // invalid
    if (const auto *intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(inst)) {
        if (intrinsic->getIntrinsicID() == llvm::Intrinsic::lifetime_end) {
            std::tie(index, area) =
                    getEqualArea(location.relations, intrinsic->getOperand(1));
            assert(area);
            validAreas[index] = false;
        }
    }

    if (const auto *call = llvm::dyn_cast<llvm::CallInst>(inst)) {
        auto *function = call->getCalledFunction();

        if (!function)
            return;

        AnalysisOptions options;
        if (options.getAllocationFunction(function->getName().str()) ==
            AllocationFunction::REALLOC) {
            // if realloc of memory occured, the reallocated memory cannot
            // be considered valid until the realloc is proven unsuccessful
            std::tie(index, area) =
                    getEqualArea(location.relations, call->getOperand(0));
            if (area)
                validAreas[index] = false;
            else if (!llvm::isa<llvm::ConstantPointerNull>(
                             call->getOperand(0))) {
                // else we do not know which area has been reallocated and
                // thus possibly invalidated, so it may have been any of
                // them
                invalidateHeapAllocatedAreas(validAreas);
            }
        }

        if (function->getName().equals("free")) {
            // if free occures, the freed memory cannot be considered valid
            // anymore
            std::tie(index, area) =
                    getEqualArea(location.relations, call->getOperand(0));

            if (area)
                validAreas[index] = false;
            else if (!llvm::isa<llvm::ConstantPointerNull>(
                             call->getOperand(0))) {
                // else we do not know which area has been freed, so it may
                // have been any of them
                invalidateHeapAllocatedAreas(validAreas);
            }
        }
    }
}

void StructureAnalyzer::setValidArea(std::vector<bool> &validAreas,
                                     const AllocatedArea *area, unsigned index,
                                     bool validateThis) const {
    unsigned preReallocIndex = 0;
    const AllocatedArea *preReallocArea = nullptr;
    if (area->getReallocatedPtr())
        std::tie(preReallocIndex, preReallocArea) =
                getAllocatedAreaFor(area->getReallocatedPtr());

    if (validateThis) {
        validAreas[index] = true;
        if (preReallocArea)
            assert(!validAreas[preReallocIndex]);

        // else the original area, if any, should be validated
    } else if (preReallocArea) {
        validAreas[preReallocIndex] = true;
        assert(!validAreas[index]);
    }
}

// if heap allocation call was just checked as successful, mark memory valid
void StructureAnalyzer::setValidAreasByAssumeBool(VRLocation &location,
                                                  std::vector<bool> &validAreas,
                                                  VRAssumeBool *assume) const {
    const auto *icmp = llvm::dyn_cast<llvm::ICmpInst>(assume->getValue());
    if (!icmp)
        return;

    const auto *c1 = llvm::dyn_cast<llvm::ConstantInt>(icmp->getOperand(0));
    const auto *c2 = llvm::dyn_cast<llvm::ConstantInt>(icmp->getOperand(1));

    // pointer must be compared to zero // TODO? or greater
    if ((c1 && c2) || (!c1 && !c2) || (!c1 && !c2->isZero()) ||
        (!c2 && !c1->isZero()))
        return;

    // get the compared parameter
    const llvm::Value *param = c1 ? icmp->getOperand(1) : icmp->getOperand(0);

    unsigned index = 0;
    const AllocatedArea *area = nullptr;

    for (const auto *equal : location.relations.getEqual(param)) {
        std::tie(index, area) = getAllocatedAreaFor(equal);
        // if compared pointer or equal belong to allocated area, this area
        // can be marked valid
        if (area)
            break;
    }
    // the compared value is not a pointer to an allocated area
    if (!area)
        return;

    llvm::ICmpInst::Predicate pred = assume->getAssumption()
                                             ? icmp->getSignedPredicate()
                                             : icmp->getInversePredicate();

    // check that predicate implies wanted relation
    switch (pred) {
    case llvm::ICmpInst::Predicate::ICMP_EQ:
        // if reallocated pointer is equal to zero, then original memory is
        // still valid
        setValidArea(validAreas, area, index, false);
        return;

    case llvm::ICmpInst::Predicate::ICMP_NE:
        // pointer is not equal to zero, therefore it a valid result of heap
        // allocation
        setValidArea(validAreas, area, index, true);
        return;

    case llvm::ICmpInst::Predicate::ICMP_ULT:
    case llvm::ICmpInst::Predicate::ICMP_SLT:
        // if zero stands right to </<=, this proves invalidity
        if (c2)
            setValidArea(validAreas, area, index, false);
        else
            setValidArea(validAreas, area, index, true);
        return;

    case llvm::ICmpInst::Predicate::ICMP_UGT:
    case llvm::ICmpInst::Predicate::ICMP_SGT:
        // if zero stands left to >/>=, this proves invalidity
        if (c1)
            setValidArea(validAreas, area, index, false);
        else
            setValidArea(validAreas, area, index, true);
        return;

    case llvm::ICmpInst::Predicate::ICMP_ULE:
    case llvm::ICmpInst::Predicate::ICMP_SLE:
    case llvm::ICmpInst::Predicate::ICMP_UGE:
    case llvm::ICmpInst::Predicate::ICMP_SGE:
        // nothing to infer here, we do not get the information, whether
        // pointer is zero or not
        return;

    default:
        assert(0 && "unreachable, would have failed in processICMP");
    }
}

void StructureAnalyzer::setValidAreasFromSinglePredecessor(
        VRLocation &location, std::vector<bool> &validAreas) const {
    // copy predecessors valid areas
    VREdge *edge = location.getPredEdge(0);
    validAreas = edge->source->relations.getValidAreas();

    // and alter them according to info from edge
    if (edge->op->isInstruction())
        setValidAreasByInstruction(
                location, validAreas,
                static_cast<VRInstruction *>(edge->op.get()));

    if (edge->op->isAssumeBool())
        setValidAreasByAssumeBool(location, validAreas,
                                  static_cast<VRAssumeBool *>(edge->op.get()));
}

bool StructureAnalyzer::trueInAll(
        const std::vector<std::vector<bool>> &validInPreds, unsigned index) {
    for (const auto &validInPred : validInPreds) {
        if (validInPred.empty() || !validInPred[index])
            return false;
    }
    return true;
}

// in returned vector, false signifies that corresponding area is
// invalidated by some of the passed instructions
std::vector<bool> StructureAnalyzer::getInvalidatedAreas(
        const std::vector<const llvm::Instruction *> &instructions) const {
    std::vector<bool> validAreas(allocatedAreas.size(), true);

    for (const llvm::Instruction *inst : instructions) {
        VRLocation &location = codeGraph.getVRLocation(inst);
        VRInstruction vrinst(inst);

        setValidAreasByInstruction(location, validAreas, &vrinst);
    }
    return validAreas;
}

void StructureAnalyzer::setValidAreasFromMultiplePredecessors(
        VRLocation &location, std::vector<bool> &validAreas) const {
    std::vector<std::vector<bool>> validInPreds;

    if (!location.isJustLoopJoin()) {
        for (VREdge *predEdge : location.predecessors)
            validInPreds.emplace_back(
                    predEdge->source->relations.getValidAreas());
    } else {
        VRLocation *treePred = nullptr;
        for (VREdge *predEdge : location.predecessors) {
            if (predEdge->type == EdgeType::TREE) {
                treePred = predEdge->source;
                break;
            }
        }
        assert(treePred);

        validInPreds.emplace_back(treePred->relations.getValidAreas());
        validInPreds.emplace_back(
                getInvalidatedAreas(inloopValues.at(&location)));
    }

    // intersect valid areas from predecessors
    for (unsigned i = 0; i < allocatedAreas.size(); ++i) {
        validAreas.push_back(trueInAll(validInPreds, i));
    }
}

void StructureAnalyzer::computeValidAreas() const {
    for (auto &location : codeGraph) {
        std::vector<bool> &validAreas = location.relations.getValidAreas();

        switch (location.predsSize()) {
        case 0:
            setValidAreasFromNoPredecessors(validAreas);
            break;
        case 1:
            setValidAreasFromSinglePredecessor(location, validAreas);
            break;
        default:
            setValidAreasFromMultiplePredecessors(location, validAreas);
            break;
        }
    }
}

void StructureAnalyzer::initializeCallRelations() {
    for (const llvm::Function &function : module) {
        if (function.isDeclaration())
            continue;

        auto pair = callRelationsMap.emplace(&function,
                                             std::vector<CallRelation>());

        // for each location, where the function is called
        for (const llvm::Value *user : function.users()) {
            // get call from user
            const llvm::CallInst *call = llvm::dyn_cast<llvm::CallInst>(user);
            if (!call)
                continue;

            std::vector<CallRelation> &callRelations = pair.first->second;

            callRelations.emplace_back();
            CallRelation &callRelation = callRelations.back();

            // set pointer to the location from which the function is called
            callRelation.callSite = &codeGraph.getVRLocation(call);

            // set formal parameters equal to real
            unsigned argCount = 0;
            for (const llvm::Argument &formalArg : function.args()) {
                if (argCount >= llvmutils::getNumArgOperands(call))
                    break;
                const llvm::Value *realArg = call->getArgOperand(argCount);

                callRelation.equalPairs.emplace_back(&formalArg, realArg);
                ++argCount;
            }
        }
    }
}

void StructureAnalyzer::analyzeBeforeRelationsAnalysis() {
    categorizeEdges();
    findLoops();
    collectInstructionSet();
    initializeCallRelations();
    // initializeDefined(m, blcs);
}

void StructureAnalyzer::analyzeAfterRelationsAnalysis() {
    collectAllocatedAreas();
    computeValidAreas();
}

bool StructureAnalyzer::isDefined(VRLocation *loc,
                                  const llvm::Value *val) const {
    if (llvm::isa<llvm::Constant>(val))
        return true;

    auto definedHere = defined.at(loc);
    return definedHere.find(val) != definedHere.end();
}

std::vector<const VREdge *>
StructureAnalyzer::possibleSources(const llvm::PHINode *phi, bool bval) const {
    std::vector<const VREdge *> result;
    for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
        const llvm::Value *val = phi->getIncomingValue(i);
        const auto *cVal = llvm::dyn_cast<llvm::ConstantInt>(val);

        if (!cVal || (cVal->isOne() && bval) || (cVal->isZero() && !bval)) {
            const auto *block = phi->getIncomingBlock(i);
            const VRLocation &end = codeGraph.getVRLocation(
                    &*std::prev(std::prev(block->end())));
            assert(end.succsSize() == 1);
            result.emplace_back(end.getSuccEdge(0));
        }
    }
    return result;
}

std::vector<const llvm::ICmpInst *>
StructureAnalyzer::getRelevantConditions(const VRAssumeBool *assume) const {
    if (const auto *icmp = llvm::dyn_cast<llvm::ICmpInst>(assume->getValue()))
        return {icmp};

    const auto *phi = llvm::dyn_cast<llvm::PHINode>(assume->getValue());
    if (!phi)
        return {};

    std::vector<const llvm::ICmpInst *> result;
    std::vector<const llvm::PHINode *> to_process{phi};

    while (!to_process.empty()) {
        const auto *phi = to_process.back();
        to_process.pop_back();

        for (const auto *sourceEdge :
             possibleSources(phi, assume->getAssumption())) {
            assert(sourceEdge->op->isInstruction());
            const llvm::Instruction *inst =
                    static_cast<const VRInstruction *>(sourceEdge->op.get())
                            ->getInstruction();
            if (const auto *icmp = llvm::dyn_cast<llvm::ICmpInst>(inst))
                result.emplace_back(icmp);
            else if (const auto *phi = llvm::dyn_cast<llvm::PHINode>(inst))
                to_process.emplace_back(phi);
            else
                return {};
        }
    }
    return result;
}

std::pair<unsigned, const AllocatedArea *>
StructureAnalyzer::getAllocatedAreaFor(const llvm::Value *ptr) const {
    unsigned i = 0;
    for (const auto &area : allocatedAreas) {
        if (area.getPtr() == ptr)
            return {i, &area};
        ++i;
    }
    return {0, nullptr};
}

const std::vector<CallRelation> &
StructureAnalyzer::getCallRelationsFor(const llvm::Instruction *inst) const {
#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR <= 7
    const llvm::Function *function = inst->getParent()->getParent();
#else
    const llvm::Function *function = inst->getFunction();
#endif
    assert(function);
    return callRelationsMap.at(function);
}

void StructureAnalyzer::addPrecondition(const llvm::Function *func,
                                        const llvm::Argument *lt,
                                        Relations::Type rel,
                                        const llvm::Value *rt) {
    preconditionsMap[func].emplace_back(lt, rel, rt);
}

bool StructureAnalyzer::hasPreconditions(const llvm::Function *func) const {
    return preconditionsMap.find(func) != preconditionsMap.end();
}

const std::vector<Precondition> &
StructureAnalyzer::getPreconditionsFor(const llvm::Function *func) const {
    assert(preconditionsMap.find(func) != preconditionsMap.end());
    return preconditionsMap.find(func)->second;
}

size_t StructureAnalyzer::addBorderValue(const llvm::Function *func,
                                         const llvm::Argument *from,
                                         const llvm::Value *stored) {
    auto &borderVals = borderValues[func];
    auto id = borderVals.size();
    borderVals.emplace_back(id, from, stored);
    return id;
}

bool StructureAnalyzer::hasBorderValues(const llvm::Function *func) const {
    return borderValues.find(func) != borderValues.end();
}

const std::vector<BorderValue> &
StructureAnalyzer::getBorderValuesFor(const llvm::Function *func) const {
    assert(hasBorderValues(func));
    return borderValues.find(func)->second;
}

BorderValue StructureAnalyzer::getBorderValueFor(const llvm::Function *func,
                                                 size_t id) const {
    for (const auto &bv : getBorderValuesFor(func)) {
        if (bv.id == id)
            return bv;
    }
    assert(0 && "unreachable");
    abort();
}

#ifndef NDEBUG
void StructureAnalyzer::dumpBorderValues(std::ostream &out) const {
    out << "[ \n";
    for (auto &foo : borderValues) {
        out << "    " << foo.first->getName().str() << ": ";
        for (auto &bv : foo.second)
            out << "("
                << "id " << bv.id << ", "
                << "from " << debug::getValName(bv.from) << ", "
                << "stored " << debug::getValName(bv.stored) << "), ";
    }
    out << "\n]\n";
}
#endif

} // namespace vr
} // namespace dg
