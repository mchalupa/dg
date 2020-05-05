#ifndef DG_LLVM_GRAPHBUILDER_H
#define DG_LLVM_GRAPHBUILDER_H

#include <map>
#include <unordered_map>
#include <vector>
#include <iosfwd>

namespace llvm {
class Value;
class Function;
class BasicBlock;
class Instruction;
class CallInst;
}

namespace dg {

class LLVMPointerAnalysis;

namespace llvmdg {

class Function;
class Block;

class GraphBuilder {

    LLVMPointerAnalysis *pointsToAnalysis_ = nullptr;
    bool threads = false;
    std::map<const llvm::Function *, Function *> _functions;
    std::unordered_map<const llvm::BasicBlock *, Block *> _mapping;

    std::vector<const llvm::Function *> getCalledFunctions(const llvm::Value *v);

    void handleCallInstruction(const llvm::Instruction *instruction,
                               Block *lastBlock,
                               bool& createBlock,
                               bool& createCallReturn);

    bool createPthreadCreate(const llvm::CallInst * callInst, Block * lastBlock);
    bool createPthreadJoin(const llvm::CallInst * callInst, Block * lastBlock);

public:

    GraphBuilder(LLVMPointerAnalysis *pointsToAnalysis = nullptr);
    ~GraphBuilder();

    Function *buildFunctionRecursively(const llvm::Function * llvmFunction);
    Function *findFunction(const llvm::Function * llvmFunction);
    Function *createOrGetFunction(const llvm::Function * llvmFunction);

    std::map<const llvm::Function *, Function *> functions() const;

    void dumpNodes(std::ostream& ostream) const;
    void dumpEdges(std::ostream& ostream) const;
    void dump(std::ostream& ostream) const;
};

// auxiliary functions
int predecessorsNumber(const llvm::BasicBlock * basicBlock);
int successorsNumber(const llvm::BasicBlock * basicBlock);
bool isReachable(const llvm::BasicBlock * basicBlock);

}
}

#endif
