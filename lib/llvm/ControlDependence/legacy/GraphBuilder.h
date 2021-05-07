#ifndef DG_LEGACY_NTSCD_GRAPHBUILDER_H
#define DG_LEGACY_NTSCD_GRAPHBUILDER_H

#include <iosfwd>
#include <map>
#include <unordered_map>
#include <vector>

namespace llvm {
class Value;
class Function;
class BasicBlock;
class Instruction;
class CallInst;
} // namespace llvm

namespace dg {

class LLVMPointerAnalysis;

namespace llvmdg {
namespace legacy {

class Function;
class Block;

class GraphBuilder {
    LLVMPointerAnalysis *pointsToAnalysis_ = nullptr;
    bool threads = false;
    std::map<const llvm::Function *, Function *> _functions;
    // mapping from llvm block to our blocks (a vector of blocks
    // since we split them apart)
    // FIXME: optimize this, most of the vectors will be just a singleton...
    std::unordered_map<const llvm::BasicBlock *, std::vector<Block *>> _mapping;

    std::vector<const llvm::Function *>
    getCalledFunctions(const llvm::Value *v);

    void handleCallInstruction(const llvm::Instruction *instruction,
                               Block *lastBlock, bool &createBlock,
                               bool &createCallReturn);

    bool createPthreadCreate(const llvm::CallInst *callInst, Block *lastBlock);
    bool createPthreadJoin(const llvm::CallInst *callInst, Block *lastBlock);

  public:
    GraphBuilder(LLVMPointerAnalysis *pointsToAnalysis = nullptr);
    ~GraphBuilder();

    Function *buildFunction(const llvm::Function *llvmFunction,
                            bool recursively = false);
    Function *findFunction(const llvm::Function *llvmFunction);
    Function *createOrGetFunction(const llvm::Function *llvmFunction);

    const std::map<const llvm::Function *, Function *> &functions() const {
        return _functions;
    }

    const std::vector<Block *> *mapBlock(const llvm::BasicBlock *b) const {
        auto it = _mapping.find(b);
        return it == _mapping.end() ? nullptr : &it->second;
    }

    void dumpNodes(std::ostream &ostream) const;
    void dumpEdges(std::ostream &ostream) const;
    void dump(std::ostream &ostream) const;
};

// auxiliary functions
int predecessorsNumber(const llvm::BasicBlock *basicBlock);
int successorsNumber(const llvm::BasicBlock *basicBlock);
bool isReachable(const llvm::BasicBlock *basicBlock);

} // namespace legacy
} // namespace llvmdg
} // namespace dg

#endif
