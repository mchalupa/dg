#include "CriticalSectionsBuilder.h"
#include "Nodes.h"
#include <llvm/IR/Instructions.h>

CriticalSectionsBuilder::CriticalSectionsBuilder()
{}

CriticalSectionsBuilder::~CriticalSectionsBuilder() {
    for (auto iterator : criticalSections_) {
        delete iterator.second;
    }
}

bool CriticalSectionsBuilder::buildCriticalSection(LockNode *lock) {
    auto iterator = criticalSections_.find(lock->callInstruction());
    if (iterator != criticalSections_.end()) {
        return false;
    }

    auto criticalSection = new CriticalSection(lock);
    criticalSections_.emplace(lock->callInstruction(), criticalSection);

    currentLock = lock;
    currentUnlocks = lock->correspondingUnlocks();
    visitNode(lock);
    bool changed = populateCriticalSection();
    examined_.clear();
    return changed;
}

bool CriticalSectionsBuilder::populateCriticalSection() {
    return criticalSections_[currentLock->callInstruction()]->insertNodes(examined_);
}

void CriticalSectionsBuilder::visitNode(Node *node) {
    preVisit(node);
    visit(node);
    postVisit(node);
}

std::set<const llvm::CallInst *> CriticalSectionsBuilder::locks() const {
    std::set<const llvm::CallInst *> llvmLocks;

    for (auto lock : criticalSections_) {
        llvmLocks.insert(lock.first);
    }
    return llvmLocks;
}

std::set<const llvm::Instruction *> CriticalSectionsBuilder::correspondingNodes(const llvm::CallInst *lock) const {
    if (!lock) {
        return {};
    }
    auto iterator = criticalSections_.find(lock);
    if (iterator != criticalSections_.end()) {
        return iterator->second->nodes();
    }
    return {};
}

std::set<const llvm::CallInst *> CriticalSectionsBuilder::correspondingUnlocks(const llvm::CallInst *lock) const {
    if (!lock) {
        return {};
    }
    auto iterator = criticalSections_.find(lock);
    if (iterator != criticalSections_.end()) {
        return iterator->second->unlocks();
    }
    return {};
}

void CriticalSectionsBuilder::preVisit(Node *node) {
    visited_.insert(node);
    if (auto unlockNode = castNode<NodeType::UNLOCK>(node)) {
        currentUnlocks.erase(unlockNode);
    }
}

void CriticalSectionsBuilder::visit(Node *node) {
    if (currentUnlocks.empty()) {
        return;
    }

    for (auto successor : *node) {
        if (!visited(successor) && !examined(successor)) {
            visitNode(successor);
        }
    }
}

void CriticalSectionsBuilder::postVisit(Node *node) {
    visited_.erase(node);
    examined_.insert(node);
}

bool CriticalSectionsBuilder::visited(Node *node) const {
    auto iterator = visited_.find(node);
    if (iterator != visited_.end()) {
        return true;
    }
    return false;
}

bool CriticalSectionsBuilder::examined(Node *node) const {
    auto iterator = examined_.find(node);
    if (iterator != examined_.end()) {
        return true;
    }
    return false;
}

CriticalSection::CriticalSection(LockNode *lock):lock_(lock)
{}

const llvm::CallInst *CriticalSection::lock() const {
    return this->lock_->callInstruction();
}

std::set<const llvm::Instruction *> CriticalSection::nodes() const {
    std::set<const llvm::Instruction *> llvmNodes;
    for (auto node : nodes_) {
        if (!node->isArtificial()) {
            llvmNodes.insert(node->llvmInstruction());
        }
    }
    return llvmNodes;
}

std::set<const llvm::CallInst *> CriticalSection::unlocks() const {
    std::set<const llvm::CallInst *> llvmUnlocks;
    for (auto unlock : lock_->correspondingUnlocks()) {
        llvmUnlocks.insert(unlock->callInstruction());
    }
    return llvmUnlocks;
}

bool CriticalSection::insertNodes(const std::set<Node *> &nodes) {
    auto sizeBefore = this->nodes_.size();
    this->nodes_.insert(nodes.begin(), nodes.end());
    this->nodes_.erase(lock_);
    auto sizeAfter = this->nodes_.size();
    return sizeBefore < sizeAfter;
}
