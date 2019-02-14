#ifndef GRAPHBUILDER_H
#define GRAPHBUILDER_H

#include <utility>
#include <set>
#include <unordered_set>
#include <unordered_map>

namespace dg {
    class LLVMPointerAnalysis;
    namespace analysis {
    namespace pta {
        class PSNodeJoin;
    }
    }
}

namespace llvm {
    class Instruction;
    class BasicBlock;
    class Function;
    class CallInst;
}

class Node;
class ForkNode;
class JoinNode;
class LockNode;
class UnlockNode;
class BlockGraph;
class FunctionGraph;

class GraphBuilder
{
private:
    dg::LLVMPointerAnalysis *                                       pointsToAnalysis_ = nullptr;

    std::unordered_set<Node *>                                      artificialNodes_;
    std::unordered_map<const llvm::Instruction *, Node *>           llvmToNodeMap_;

    std::unordered_map<const llvm::BasicBlock *, BlockGraph *>      llvmToBlockMap_;
    std::unordered_map<const llvm::Function *, FunctionGraph *>     llvmToFunctionMap_;

    std::unordered_map<const llvm::CallInst *, JoinNode *>          llvmToJoins_;
    std::unordered_map<const llvm::CallInst *, ForkNode *>          llvmToForks_;

    std::unordered_map<const llvm::CallInst *, LockNode *>          llvmToLocks_;
    std::unordered_map<const llvm::CallInst *, UnlockNode *>        llvmToUnlocks_;

public:
    using NodeSequence = std::pair<Node *, Node *>;

    GraphBuilder(dg::LLVMPointerAnalysis * pointsToAnalysis);

    ~GraphBuilder();

    auto size() const -> decltype (llvmToNodeMap_.size() + artificialNodes_.size()) {
        return llvmToNodeMap_.size() + artificialNodes_.size();
    }

    NodeSequence buildInstruction(const llvm::Instruction * instruction);

    NodeSequence buildBlock(const llvm::BasicBlock * basicBlock);

    NodeSequence buildFunction(const llvm::Function * function);

    Node * findInstruction(const llvm::Instruction * instruction);

    BlockGraph * findBlock(const llvm::BasicBlock * basicBlock);

    FunctionGraph * findFunction(const llvm::Function * function);

    std::set<const llvm::CallInst *> getJoins() const;
    std::set<const llvm::CallInst *> getCorrespondingForks(const llvm::CallInst *callInst) const;

    std::set<LockNode *> getLocks() const;

    bool matchForksAndJoins();

    bool matchLocksAndUnlocks();

    void print(std::ostream & ostream) const;

    void printNodes(std::ostream & ostream) const;

    void printEdges(std::ostream & ostream) const;

    void clear();

private:
    NodeSequence buildCallInstruction(const llvm::Instruction * instruction);

    NodeSequence buildReturnInstruction(const llvm::Instruction * instruction);

    NodeSequence buildGeneralInstruction(const llvm::Instruction * instruction);

    NodeSequence buildGeneralCallInstruction(const llvm::CallInst * callInstruction);

    NodeSequence insertFunction(const llvm::Function * function, const llvm::CallInst *callInstruction);

    NodeSequence insertFunctionPointerCall(const llvm::CallInst *callInstruction);

    NodeSequence insertUndefinedFunction(const llvm::Function * function, const llvm::CallInst *callInstruction);

    NodeSequence insertPthreadCreate(const llvm::CallInst * callInstruction);

    NodeSequence insertPthreadMutexLock(const llvm::CallInst * callInstruction);

    NodeSequence insertPthreadMutexUnlock(const llvm::CallInst * callInstruction);

    NodeSequence insertPthreadJoin(const llvm::CallInst * callInstruction);

    NodeSequence insertPthreadExit(const llvm::CallInst * callInstruction);

    NodeSequence createOrGetFunction(const llvm::Function * function);

    bool populateCorrespondingForks(JoinNode * join, dg::analysis::pta::PSNodeJoin *PSJoin);

    bool connectJoins(JoinNode * join, dg::analysis::pta::PSNodeJoin *PSJoin);

    template <typename T>
    T * addNode(T * node) {
        if (node->isArtificial()) {
            if (this->artificialNodes_.insert(node).second) {
                return node;
            }
        } else {
            if (this->llvmToNodeMap_.emplace(node->llvmInstruction(), node).second) {
                return node;
            }
        }
        return nullptr;
    }
};

int predecessorsNumber(const llvm::BasicBlock * basicBlock);

int successorsNumber(const llvm::BasicBlock * basicBlock);

bool isReachable(const llvm::BasicBlock * basicBlock);

template <typename T>
const llvm::CallInst *getCallInst(T *PSNode) {
    return PSNode->callInst()->template getUserData<llvm::CallInst>();
}

#endif // GRAPHBUILDER_H
