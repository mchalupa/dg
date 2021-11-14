#include "dg/llvm/ValueRelations/GraphElements.h"

#include <cassert>
#include <iterator>
#include <list>
#include <queue>

#include "dg/llvm/ValueRelations/UniquePtrVector.h"
#include "dg/llvm/ValueRelations/ValueRelations.h"

#include <llvm/IR/Instructions.h>

#ifndef NDEBUG
#include "dg/llvm/ValueRelations/getValName.h"
#endif

namespace dg {
namespace vr {

void VRLocation::connect(std::unique_ptr<VREdge> &&edge) {
    if (edge->target)
        edge->target->predecessors.push_back(edge.get());
    successors.emplace_back(std::move(edge));
}

void VRLocation::connect(VRLocation *target, std::unique_ptr<VROp> &&op) {
    connect(std::unique_ptr<VREdge>(new VREdge(this, target, std::move(op))));
}

void VRLocation::connect(VRLocation *target, VROp *op) {
    connect(std::unique_ptr<VREdge>(new VREdge(this, target, op)));
}

void VRLocation::connect(VRLocation &target, VROp *op) { connect(&target, op); }

std::vector<VREdge *> VRLocation::getPredecessors() { return predecessors; }

std::vector<VREdge *>
VRLocation::getSuccessors() { // TODO create an iterator to unwrap the unique
                              // pointers
    std::vector<VREdge *> result;
    for (auto &succ : successors) {
        result.push_back(succ.get());
    }
    return result;
}

std::vector<VRLocation *> VRLocation::getPredLocations() {
    std::vector<VRLocation *> result;
    for (VREdge *edge : predecessors) {
        result.push_back(edge->source);
    }
    return result;
}

std::vector<VRLocation *> VRLocation::getSuccLocations() {
    std::vector<VRLocation *> result;
    for (auto &edge : successors) {
        result.push_back(edge->target);
    }
    return result;
}

bool VRLocation::isJoin() const { return predecessors.size() > 1; }

bool VRLocation::isJustBranchJoin() const {
    // allows TREE and FORWARD
    if (!isJoin())
        return false;
    for (VREdge *pred : predecessors) {
        if (pred->type != EdgeType::TREE && pred->type != EdgeType::FORWARD)
            return false;
    }
    return true;
}

bool VRLocation::isJustLoopJoin() const {
    // allows TREE and BACK
    if (!isJoin())
        return false;
    for (VREdge *pred : predecessors) {
        if (pred->type != EdgeType::TREE && pred->type != EdgeType::BACK)
            return false;
    }
    return true;
}

VRLocation &VRLocation::getTreePredecessor() const {
    VRLocation *treePred = nullptr;
    for (VREdge *predEdge : predecessors) {
        if (predEdge->type == EdgeType::TREE)
            treePred = predEdge->source;
    }
    assert(treePred);
    return *treePred;
}

VRLocation &VRCodeGraph::newVRLocation() {
    locations.emplace_back(locations.size());
    return locations.back();
}

VRLocation &VRCodeGraph::newVRLocation(const llvm::Instruction *inst) {
    assert(locationMapping.find(inst) == locationMapping.end());

    VRLocation &loc = newVRLocation();
    locationMapping.emplace(inst, &loc);
    return loc;
}

void VRCodeGraph::setEntryLocation(const llvm::Function *f, VRLocation &loc) {
    functionMapping.emplace(f, &loc);
}

VRLocation &VRCodeGraph::getVRLocation(const llvm::Instruction *ptr) const {
    return *locationMapping.at(ptr);
}

VRLocation &VRCodeGraph::getEntryLocation(const llvm::Function *f) const {
    return *functionMapping.at(f);
}

void VRCodeGraph::hasCategorizedEdges() { categorizedEdges = true; }

/* ************ function iterator stuff ************ */

VRCodeGraph::Supplements::Supplements(const llvm::Function *f,
                                      VRLocation *start, Dir d)
        : function(f), direction(d) {
    visited.emplace(start);
}

std::vector<VREdge *>
VRCodeGraph::Supplements::getNextEdges(VRLocation *loc) const {
    return direction == Dir::FORWARD ? loc->getSuccessors()
                                     : loc->getPredecessors();
}

VRLocation *VRCodeGraph::Supplements::getNextLocation(VREdge *edge) const {
    return direction == Dir::FORWARD ? edge->target : edge->source;
}

bool VRCodeGraph::Supplements::inOtherFunction(VREdge *edge) const {
    if (edge->op->isInstruction()) {
        const llvm::Instruction *inst =
                static_cast<VRInstruction *>(edge->op.get())->getInstruction();
        if (inst->getFunction() != function) {
            assert(0 && "has edge to other function");
            return true;
        }
    }
    return false;
}

// is null or target was visited or leads to other function
bool VRCodeGraph::Supplements::irrelevant(VREdge *edge) const {
    VRLocation *next = getNextLocation(edge);
    return !next || visited.find(next) != visited.end() ||
           inOtherFunction(edge);
}

VRCodeGraph::BFSIncrement::BFSIncrement(const llvm::Function *f,
                                        VRLocation *start, bool e, Dir d)
        : Supplements(f, start, d), categorizedEdges(e) {
    queue.emplace(start, nullptr);
}

void VRCodeGraph::BFSIncrement::increment() {
    VRLocation *current = queue.front().first;
    queue.pop();

    for (VREdge *edge : getNextEdges(current)) {
        // do not explore if there is no target or if target was already
        // explored or if is in other function
        if (irrelevant(edge))
            continue;

        VRLocation *next = getNextLocation(edge);

        // if there is still other unexplored path to the join, then
        // wait untill it is explored also
        if (direction == Dir::FORWARD && categorizedEdges &&
            next->isJustBranchJoin()) {
            auto pair = counts.emplace(next, 0);

            unsigned &targetFoundTimes = pair.first->second;
            ++targetFoundTimes;
            if (targetFoundTimes != next->predecessors.size())
                continue;
        }

        // otherwise set the target to be explored
        queue.emplace(next, edge);
        visited.emplace(next);
    }
}

VRCodeGraph::DFSIncrement::DFSIncrement(const llvm::Function *f,
                                        VRLocation *start, bool /*e*/, Dir d)
        : Supplements(f, start, d) {
    stack.emplace_back(start, 0, nullptr);
}

void VRCodeGraph::DFSIncrement::increment() {
    while (!stack.empty()) {
        VRLocation *current;
        unsigned index;
        VREdge *prevEdge;
        std::tie(current, index, prevEdge) = stack.back();
        stack.pop_back();

        std::vector<VREdge *> nextEdges = getNextEdges(current);
        // do not explore if there is no target or if target was already
        // explored or if is in other function
        while (index < nextEdges.size() && irrelevant(nextEdges[index]))
            ++index;

        if (index >= nextEdges.size())
            continue;
        stack.emplace_back(current, index + 1, prevEdge);

        VREdge *nextEdge = nextEdges[index];
        VRLocation *next = getNextLocation(nextEdge);

        // otherwise set the target to be explored
        stack.emplace_back(next, 0, nextEdge);
        visited.emplace(next);
        return;
    }
}

bool VRCodeGraph::DFSIncrement::onStack(VRLocation *loc) const {
    for (auto &elem : stack)
        if (loc == std::get<0>(elem))
            return true;
    return false;
}

bool VRCodeGraph::DFSIncrement::wasVisited(VRLocation *loc) const {
    return visited.find(loc) != visited.end();
}

/* ************ code graph iterator stuff ************ */

VRCodeGraph::VRCodeGraphIterator::VRCodeGraphIterator(MappingIterator end)
        : intoMapping(end), endMapping(end) {}

VRCodeGraph::VRCodeGraphIterator::VRCodeGraphIterator(MappingIterator begin,
                                                      MappingIterator end,
                                                      bool e)
        : intoMapping(begin), endMapping(end),
          intoFunction(begin->first, begin->second, e, Dir::FORWARD),
          categorizedEdges(e) {}

VRCodeGraph::VRCodeGraphIterator &
VRCodeGraph::VRCodeGraphIterator::operator++() {
    ++intoFunction;
    if (intoFunction == BFSIterator()) {
        ++intoMapping;
        if (intoMapping != endMapping)
            intoFunction = BFSIterator(intoMapping->first, intoMapping->second,
                                       categorizedEdges, Dir::FORWARD);
    }
    return *this;
}

VRCodeGraph::VRCodeGraphIterator
VRCodeGraph::VRCodeGraphIterator::operator++(int) {
    auto copy = *this;
    ++*this;
    return copy;
}

} // namespace vr
} // namespace dg