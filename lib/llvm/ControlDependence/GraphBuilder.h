#ifndef DG_LLVM_GRAPHBUILDER_H
#define DG_LLVM_GRAPHBUILDER_H

#include <map>
#include <iosfwd>

namespace llvm {

class Function;
class BasicBlock;
class Instruction;
class CallInst;

}

namespace dg {

class LLVMPointerAnalysis;

namespace cd {

class Function;
class Block;

class GraphBuilder
{
public:

    GraphBuilder(LLVMPointerAnalysis * pointsToAnalysis);

    ~GraphBuilder();

    Function * buildFunctionRecursively(const llvm::Function * llvmFunction);

    Function * findFunction(const llvm::Function * llvmFunction);

    Function * createOrGetFunction(const llvm::Function * llvmFunction);

    std::map<const llvm::Function *, Function *> functions() const;

    void dumpNodes(std::ostream & ostream) const;

    void dumpEdges(std::ostream & ostream) const;

    void dump(std::ostream & ostream) const;

private:

    LLVMPointerAnalysis *                         pointsToAnalysis_   = nullptr;
    bool                                            threads             = false;
    std::map<const llvm::Function *, Function *>    functions_;

    void handleCallInstruction(const llvm::Instruction * instruction, Block * lastBlock, bool & createBlock, bool & createCallReturn);

    bool createPthreadCreate(const llvm::CallInst * callInst, Block * lastBlock);

    bool createPthreadJoin(const llvm::CallInst * callInst, Block * lastBlock);
};

int predecessorsNumber(const llvm::BasicBlock * basicBlock);

int successorsNumber(const llvm::BasicBlock * basicBlock);

bool isReachable(const llvm::BasicBlock * basicBlock);

}
}

#endif // DG_LLVM_GRAPHBUILDER_H
