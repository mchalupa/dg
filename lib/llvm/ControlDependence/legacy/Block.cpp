#include "Block.h"
#include "Function.h"

#include <sstream>

#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>

namespace dg {
namespace llvmdg {
namespace legacy {

int Block::traversalCounter = 0;

const std::set<Block *> &Block::predecessors() const { return predecessors_; }

const std::set<Block *> &Block::successors() const { return successors_; }

bool Block::addPredecessor(Block *predecessor) {
    if (!predecessor) {
        return false;
    }
    predecessors_.insert(predecessor);
    return predecessor->successors_.insert(this).second;
}

bool Block::removePredecessor(Block *predecessor) {
    if (!predecessor) {
        return false;
    }
    predecessors_.erase(predecessor);
    return predecessor->successors_.erase(this);
}

bool Block::addSuccessor(Block *successor) {
    if (!successor) {
        return false;
    }
    successors_.insert(successor);
    return successor->predecessors_.insert(this).second;
}

bool Block::removeSuccessor(Block *successor) {
    if (!successor) {
        return false;
    }
    successors_.erase(successor);
    return successor->predecessors_.erase(this);
}

const llvm::Instruction *Block::lastInstruction() const {
    if (!llvmInstructions_.empty()) {
        return llvmInstructions_.back();
    }
    return nullptr;
}

bool Block::addInstruction(const llvm::Instruction *instruction) {
    if (!instruction) {
        return false;
    }
    llvmInstructions_.push_back(instruction);
    return true;
}

bool Block::addCallee(const llvm::Function *llvmFunction, Function *function) {
    if (!llvmFunction || !function) {
        return false;
    }
    return callees_.emplace(llvmFunction, function).second;
}

bool Block::addFork(const llvm::Function *llvmFunction, Function *function) {
    if (!llvmFunction || !function) {
        return false;
    }
    return forks_.emplace(llvmFunction, function).second;
}

bool Block::addJoin(const llvm::Function *llvmFunction, Function *function) {
    if (!llvmFunction || !function) {
        return false;
    }
    return joins_.emplace(llvmFunction, function).second;
}

const std::map<const llvm::Function *, Function *> &Block::callees() const {
    return callees_;
}

std::map<const llvm::Function *, Function *> Block::callees() {
    return callees_;
}

const std::map<const llvm::Function *, Function *> &Block::forks() const {
    return forks_;
}

std::map<const llvm::Function *, Function *> Block::forks() { return forks_; }

const std::map<const llvm::Function *, Function *> &Block::joins() const {
    return joins_;
}

std::map<const llvm::Function *, Function *> Block::joins() { return joins_; }

bool Block::isCall() const {
    return !llvmInstructions_.empty() &&
           llvmInstructions_.back()->getOpcode() == llvm::Instruction::Call;
}

bool Block::isArtificial() const { return llvmInstructions_.empty(); }

bool Block::isCallReturn() const { return isArtificial() && callReturn; }

bool Block::isExit() const { return isArtificial() && !isCallReturn(); }

std::string Block::dotName() const {
    std::stringstream stream;
    stream << "NODE" << this;
    return stream.str();
}

std::string Block::label() const {
    std::string label_ = "[label=\"";
    if (llvmBlock()) {
        label_ += "Function: ";
        label_ += llvmBlock()->getParent()->getName();
    }
    label_ += "\\n\\nid:";
    label_ += std::to_string(traversalId_);
    if (isCallReturn()) {
        label_ += " Call Return Block\\n\\n";
    } else if (isArtificial()) {
        label_ += " Unified Exit Block\\n\\n";
    } else {
        label_ += " Block\\n\\n";
        std::string llvmTemporaryString;
        llvm::raw_string_ostream llvmStream(llvmTemporaryString);
        for (const auto *instruction : llvmInstructions_) {
            instruction->print(llvmStream);
            label_ += llvmTemporaryString + "\\n";
            llvmTemporaryString.clear();
        }
    }
    label_ += "\", shape=box]";
    return label_;
}

void Block::visit() {
    this->traversalId();
    for (auto *successor : successors_) {
        if (successor->bfsId() == 0) {
            successor->visit();
        }
    }
    this->traversalId();
}

void Block::dumpNode(std::ostream &ostream) const {
    ostream << dotName() << " " << label();
}

void Block::dumpEdges(std::ostream &ostream) const {
    for (auto *successor : successors_) {
        ostream << this->dotName() << " -> " << successor->dotName() << "\n";
    }

    for (auto callee : callees_) {
        ostream << this->dotName() << " -> "
                << callee.second->entry()->dotName()
                << " [style=dashed, constraint=false]\n";
        ostream << callee.second->exit()->dotName() << " -> " << this->dotName()
                << " [style=dashed, constraint=false]\n";
    }

    for (auto fork : forks_) {
        ostream << this->dotName() << " -> " << fork.second->entry()->dotName()
                << " [style=dotted, constraint=false]\n";
    }

    for (auto join : joins_) {
        ostream << join.second->exit()->dotName() << " -> " << this->dotName()
                << " [style=dotted, constraint=false]\n";
    }
}

} // namespace legacy
} // namespace llvmdg
} // namespace dg
