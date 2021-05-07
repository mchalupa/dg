#ifndef DG_LEGACY_NTSCD_BLOCK_H
#define DG_LEGACY_NTSCD_BLOCK_H

#include <iosfwd>
#include <map>
#include <set>
#include <vector>

namespace llvm {
class Instruction;
class Function;
class BasicBlock;
} // namespace llvm

namespace dg {
namespace llvmdg {
namespace legacy {

class Function;

class Block {
    const llvm::BasicBlock *_llvm_blk;

  public:
    Block(const llvm::BasicBlock *b, bool callReturn = false)
            : _llvm_blk(b), callReturn(callReturn) {}

    // FIXME: make vector
    const std::set<Block *> &predecessors() const;
    const std::set<Block *> &successors() const;

    bool addPredecessor(Block *predecessor);
    bool removePredecessor(Block *predecessor);

    bool addSuccessor(Block *successor);
    bool removeSuccessor(Block *successor);

    const std::vector<const llvm::Instruction *> &llvmInstructions() const {
        return llvmInstructions_;
    }

    const llvm::Instruction *lastInstruction() const;

    bool addInstruction(const llvm::Instruction *instruction);

    bool addCallee(const llvm::Function *llvmFunction, Function *function);
    bool addFork(const llvm::Function *llvmFunction, Function *function);
    bool addJoin(const llvm::Function *llvmFunction, Function *function);

    const std::map<const llvm::Function *, Function *> &callees() const;
    std::map<const llvm::Function *, Function *> callees();

    const std::map<const llvm::Function *, Function *> &forks() const;
    std::map<const llvm::Function *, Function *> forks();

    const std::map<const llvm::Function *, Function *> &joins() const;
    std::map<const llvm::Function *, Function *> joins();

    bool isCall() const;
    bool isArtificial() const;
    bool isCallReturn() const;
    bool isExit() const;

    void traversalId() { traversalId_ = ++traversalCounter; }
    int bfsId() const { return traversalId_; }

    const llvm::BasicBlock *llvmBlock() const { return _llvm_blk; }

    std::string dotName() const;

    std::string label() const;

    void visit();

    void dumpNode(std::ostream &ostream) const;
    void dumpEdges(std::ostream &ostream) const;

  private:
    static int traversalCounter;

    std::vector<const llvm::Instruction *> llvmInstructions_;

    std::set<Block *> predecessors_;
    std::set<Block *> successors_;

    bool callReturn = false;
    int traversalId_ = 0;

    std::map<const llvm::Function *, Function *> callees_;
    std::map<const llvm::Function *, Function *> forks_;
    std::map<const llvm::Function *, Function *> joins_;
};

} // namespace legacy
} // namespace llvmdg
} // namespace dg

#endif // DG_LLVM_BLOCK_H
