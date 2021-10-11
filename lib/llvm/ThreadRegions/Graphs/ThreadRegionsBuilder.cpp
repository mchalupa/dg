#include "ThreadRegionsBuilder.h"

#include "dg/llvm/ThreadRegions/ThreadRegion.h"
#include "llvm/ThreadRegions/Nodes/Nodes.h"

ThreadRegionsBuilder::ThreadRegionsBuilder(std::size_t size)
        : visitedNodeToRegionMap(size), examinedNodeToRegionMap(size) {}

ThreadRegionsBuilder::~ThreadRegionsBuilder() { clear(); }

void ThreadRegionsBuilder::build(Node *node) {
    auto *threadRegion = new ThreadRegion(node);
    threadRegions_.insert(threadRegion);
    this->visitedNodeToRegionMap.emplace(node, threadRegion);

    visit(node);
    //    visitRightNode(node);
    populateThreadRegions();
    clearComputingData();
}

void ThreadRegionsBuilder::populateThreadRegions() {
    for (auto nodeAngRegion : examinedNodeToRegionMap) {
        nodeAngRegion.second->insertNode(nodeAngRegion.first);
    }
}

bool ThreadRegionsBuilder::unvisited(Node *node) const {
    return !regionOfVisitedNode(node) && !regionOfExaminedNode(node);
}

bool ThreadRegionsBuilder::visited(Node *node) const {
    return regionOfVisitedNode(node);
}

bool ThreadRegionsBuilder::examined(Node *node) const {
    return regionOfExaminedNode(node);
}

void ThreadRegionsBuilder::visit(Node *node) {
    for (auto *successor : *node) {
        if (visited(successor)) {
            continue;
        }
        if (examined(region(successor))) {
            region(node)->addSuccessor(region(successor));
        } else {
            ThreadRegion *successorRegion = nullptr;
            if (shouldCreateNewRegion(node, successor)) {
                successorRegion = new ThreadRegion(successor);
                threadRegions_.insert(successorRegion);
                region(node)->addSuccessor(successorRegion);
            } else {
                successorRegion = region(node);
            }
            this->visitedNodeToRegionMap.emplace(successor, successorRegion);
            visit(successor);
        }
    }
    this->examinedNodeToRegionMap.emplace(node, region(node));
    this->visitedNodeToRegionMap.erase(node);
}

bool ThreadRegionsBuilder::examined(ThreadRegion *region) const {
    if (!region) {
        return false;
    }
    return examined(region->foundingNode());
}

ThreadRegion *ThreadRegionsBuilder::region(Node *node) const {
    auto *threadRegion = regionOfExaminedNode(node);
    if (!threadRegion) {
        threadRegion = regionOfVisitedNode(node);
    }
    return threadRegion;
}

void ThreadRegionsBuilder::printNodes(std::ostream &ostream) const {
    for (auto *threadRegion : threadRegions_) {
        threadRegion->printNodes(ostream);
    }
}

void ThreadRegionsBuilder::printEdges(std::ostream &ostream) const {
    for (auto *threadRegion : threadRegions_) {
        threadRegion->printEdges(ostream);
    }
}

void ThreadRegionsBuilder::reserve(std::size_t size) {
    visitedNodeToRegionMap.reserve(size);
    examinedNodeToRegionMap.reserve(size);
}

void ThreadRegionsBuilder::clear() {
    for (auto iterator : visitedNodeToRegionMap) {
        delete iterator.second;
    }

    for (auto iterator : examinedNodeToRegionMap) {
        delete iterator.second;
    }

    clearComputingData();

    for (auto *iterator : threadRegions_) {
        delete iterator;
    }

    threadRegions_.clear();
}

std::set<ThreadRegion *> ThreadRegionsBuilder::threadRegions() {
    return threadRegions_;
}

void ThreadRegionsBuilder::clearComputingData() {
    visitedNodeToRegionMap.clear();
    examinedNodeToRegionMap.clear();
}

ThreadRegion *ThreadRegionsBuilder::regionOfVisitedNode(Node *node) const {
    auto iterator = visitedNodeToRegionMap.find(node);
    if (iterator != visitedNodeToRegionMap.end()) {
        return iterator->second;
    }
    return nullptr;
}

ThreadRegion *ThreadRegionsBuilder::regionOfExaminedNode(Node *node) const {
    auto iterator = examinedNodeToRegionMap.find(node);
    if (iterator != visitedNodeToRegionMap.end()) {
        return iterator->second;
    }
    return nullptr;
}

bool ThreadRegionsBuilder::shouldCreateNewRegion(Node *caller,
                                                 Node *successor) const {
    return caller->getType() == NodeType::EXIT ||
           caller->getType() == NodeType::FORK ||
           successor->getType() == NodeType::ENTRY ||
           successor->getType() == NodeType::JOIN ||
           successor->predecessorsNumber() > 1;
}
