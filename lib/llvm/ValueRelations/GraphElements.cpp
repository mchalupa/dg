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

VRLocation &VRCodeGraph::getEntryLocation(const llvm::Function &f) const {
    return *functionMapping.at(&f);
}

void VRCodeGraph::hasCategorizedEdges() { categorizedEdges = true; }

/* ************ visits ************ */

void VRCodeGraph::SimpleVisit::find(VRLocation *loc) { visited.emplace(loc); }

bool VRCodeGraph::SimpleVisit::wasVisited(VRLocation *loc) const {
    return visited.find(loc) != visited.end();
}

unsigned VRCodeGraph::LazyVisit::getPrevEdgesSize(VRLocation *loc) {
    return loc->predsSize();
}

void VRCodeGraph::LazyVisit::find(VRLocation *loc) {
    auto &count = visited[loc];
    if (loc->isJustBranchJoin())
        ++count;
    else
        count = getPrevEdgesSize(loc);
}

bool VRCodeGraph::LazyVisit::wasVisited(VRLocation *loc) const {
    auto it = visited.find(loc);
    return it != visited.end() && it->second >= getPrevEdgesSize(loc);
}

/* ************ begins and ends ************ */

VRCodeGraph::LazyDFS
VRCodeGraph::lazy_dfs_begin(const llvm::Function &f) const {
    assert(categorizedEdges);
    return LazyDFS(f, &getEntryLocation(f), Dir::FORWARD);
}

VRCodeGraph::LazyDFS
VRCodeGraph::lazy_dfs_begin(const llvm::Function &f,
                            const VRLocation &start) const {
    assert(categorizedEdges);
    return LazyDFS(f, &start, Dir::FORWARD);
}

VRCodeGraph::LazyDFS VRCodeGraph::lazy_dfs_end() { return LazyDFS(); }

VRCodeGraph::SimpleDFS VRCodeGraph::dfs_begin(const llvm::Function &f) const {
    return SimpleDFS(f, &getEntryLocation(f), Dir::FORWARD);
}

VRCodeGraph::SimpleDFS VRCodeGraph::dfs_begin(const llvm::Function &f,
                                              const VRLocation &start) const {
    return SimpleDFS(f, &start, Dir::FORWARD);
}

VRCodeGraph::SimpleDFS VRCodeGraph::dfs_end() { return SimpleDFS(); }

VRCodeGraph::SimpleDFS
VRCodeGraph::backward_dfs_begin(const llvm::Function &f,
                                const VRLocation &start) {
    return SimpleDFS(f, &start, Dir::BACKWARD);
}

VRCodeGraph::SimpleDFS VRCodeGraph::backward_dfs_end() { return SimpleDFS(); }

/* ************ code graph iterator stuff ************ */

VRCodeGraph::VRCodeGraphIterator::VRCodeGraphIterator(MappingIterator end)
        : intoMapping(end), endMapping(end) {}

VRCodeGraph::VRCodeGraphIterator::VRCodeGraphIterator(MappingIterator begin,
                                                      MappingIterator end)
        : intoMapping(begin), endMapping(end),
          intoFunction(*begin->first, begin->second, Dir::FORWARD) {}

bool operator==(const VRCodeGraph::VRCodeGraphIterator &lt,
                const VRCodeGraph::VRCodeGraphIterator &rt) {
    bool ltIsEnd = lt.intoMapping == lt.endMapping;
    bool rtIsEnd = rt.intoMapping == rt.endMapping;
    return (ltIsEnd && rtIsEnd) ||
           (!ltIsEnd && !rtIsEnd && lt.intoFunction == rt.intoFunction);
}

VRCodeGraph::VRCodeGraphIterator &
VRCodeGraph::VRCodeGraphIterator::operator++() {
    ++intoFunction;
    if (intoFunction == LazyDFS()) {
        ++intoMapping;
        if (intoMapping != endMapping)
            intoFunction = LazyDFS(*intoMapping->first, intoMapping->second,
                                   Dir::FORWARD);
    }
    return *this;
}

VRCodeGraph::VRCodeGraphIterator
VRCodeGraph::VRCodeGraphIterator::operator++(int) {
    auto copy = *this;
    ++*this;
    return copy;
}

VRCodeGraph::VRCodeGraphIterator VRCodeGraph::begin() const {
    return functionMapping.empty()
                   ? VRCodeGraphIterator()
                   : VRCodeGraphIterator(functionMapping.begin(),
                                         functionMapping.end());
}

VRCodeGraph::VRCodeGraphIterator VRCodeGraph::end() const {
    return functionMapping.empty() ? VRCodeGraphIterator()
                                   : VRCodeGraphIterator(functionMapping.end());
}

} // namespace vr
} // namespace dg
