#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/CFG.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Pass.h>
SILENCE_LLVM_WARNINGS_POP

#include "llvm/ForkJoin/ForkJoin.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

#include "GraphBuilder.h"
#include "Function.h"
#include "Block.h"
#include "TarjanAnalysis.h"

using dg::ForkJoinAnalysis;

namespace dg {
namespace llvmdg {
namespace legacy {

GraphBuilder::GraphBuilder(dg::LLVMPointerAnalysis *pointsToAnalysis)
    :pointsToAnalysis_(pointsToAnalysis),
     threads(pointsToAnalysis ? pointsToAnalysis->getOptions().threads : false) {}

GraphBuilder::~GraphBuilder() {
    for (auto function : _functions) {
        delete function.second;
    }
}

bool isExit(const TarjanAnalysis<Block>::StronglyConnectedComponent * component, const Function * function) {
    bool ret = component->nodes().size() == 1;
    if (!ret) {
        return false;
    }
    return component->nodes().back() == function->exit();
}

Function *GraphBuilder::buildFunction(const llvm::Function *llvmFunction,
                                      bool recursively) {
    if (!llvmFunction) {
        return nullptr;
    }
    auto iterator = _functions.find(llvmFunction);
    if (iterator != _functions.end()) {
        return nullptr;
    }
    iterator = _functions.emplace(llvmFunction, new Function()).first;
    Function * function = iterator->second;

    std::map<const llvm::Instruction *, Block *> instToBlockMap;
    Block * lastBlock = nullptr;
    for (auto & llvmBlock : *llvmFunction) {
        if (!isReachable(&llvmBlock)) {
            continue;
        }

        bool createBlock = true;
        for (auto& llvmInst : llvmBlock) {
            if (createBlock) {
                auto tmpBlock = new Block(&llvmBlock);
                _mapping[&llvmBlock].push_back(tmpBlock);
                function->addBlock(tmpBlock);
                if (lastBlock) {
                    if (lastBlock->llvmBlock() == &llvmBlock) {
                        lastBlock->addSuccessor(tmpBlock);
                    }
                }
                lastBlock = tmpBlock;
                createBlock = false;
            }
            bool createCallReturn = false;
            if (recursively && llvmInst.getOpcode() == llvm::Instruction::Call) {
                handleCallInstruction(&llvmInst, lastBlock, createBlock, createCallReturn);
            }
            lastBlock->addInstruction(&llvmInst);
            instToBlockMap.emplace(&llvmInst, lastBlock);

            if (createCallReturn) {
                auto tmpBlock = new Block(&llvmBlock, createCallReturn);
                _mapping[&llvmBlock].push_back(tmpBlock);
                function->addBlock(tmpBlock);
                lastBlock->addSuccessor(tmpBlock);
                lastBlock = tmpBlock;
                createBlock = true;
            }
        }
    }

    for (auto & llvmBlock : *llvmFunction) {
        if (isReachable(&llvmBlock)) {
            auto block = instToBlockMap[&llvmBlock.back()];
            for (auto succ = llvm::succ_begin(&llvmBlock); succ != llvm::succ_end(&llvmBlock); ++succ) {
                auto succ_block = instToBlockMap[&succ->front()];
                block->addSuccessor(succ_block);
            }
            if (successorsNumber(&llvmBlock) == 0) {
                block->addSuccessor(function->exit());
            }
        }
    }

    TarjanAnalysis<Block> tarjan(function->nodes().size());
    tarjan.compute(function->entry());
    tarjan.computeCondensation();
    const auto & componentss = tarjan.components();
    for (const auto component : componentss) {
        if (!isExit(component, function) && component->successors().size() == 0) {
            component->nodes().back()->addSuccessor(function->exit());
        }
    }

//    auto components = tarjan.computeBackWardReachability(function->exit());

//    for (auto component : components) {
//        if (component->nodes().size() > 1 ||
//            component->successors().find(component) != component->successors().end()) {
//            component->nodes().back()->addSuccessor(function->exit());
//        }
//    }

    return function;
}

Function *GraphBuilder::findFunction(const llvm::Function *llvmFunction) {
    if (!llvmFunction) {
        return nullptr;
    }
    auto iterator = _functions.find(llvmFunction);
    if (iterator != _functions.end()) {
        return iterator->second;
    }
    return nullptr;
}

Function *GraphBuilder::createOrGetFunction(const llvm::Function *llvmFunction) {
    if (!llvmFunction) {
        return nullptr;
    }
    auto function = findFunction(llvmFunction);
    if (!function) {
        function = buildFunction(llvmFunction);
    }
    return  function;
}

void GraphBuilder::dumpNodes(std::ostream &ostream) const {
    for (auto function : _functions) {
        function.second->dumpBlocks(ostream);
    }

}

void GraphBuilder::dumpEdges(std::ostream &ostream) const {
    for (auto function : _functions) {
        function.second->dumpEdges(ostream);
    }
}

void GraphBuilder::dump(std::ostream &ostream) const {
    ostream << "digraph \"BlockGraph\" {\n";
    dumpNodes(ostream);
    dumpEdges(ostream);
    ostream << "}\n";
}

std::vector<const llvm::Function *>
GraphBuilder::getCalledFunctions(const llvm::Value *v) {
    if (auto* F = llvm::dyn_cast<llvm::Function>(v)) {
        return {F};
    }

    if (!pointsToAnalysis_)
        return {};

    return dg::getCalledFunctions(v, pointsToAnalysis_);
}

void GraphBuilder::handleCallInstruction(const llvm::Instruction *instruction,
                                         Block *lastBlock,
                                         bool &createBlock,
                                         bool &createCallReturn) {
    auto * callInst = llvm::dyn_cast<llvm::CallInst>(instruction);
#if LLVM_VERSION_MAJOR >= 8
    auto * val = callInst->getCalledOperand();
#else
    auto * val = callInst->getCalledValue();
#endif
    auto llvmFunctions = getCalledFunctions(val);

    for (auto llvmFunction : llvmFunctions) {
        if (llvmFunction->size() > 0) {
            auto function = createOrGetFunction(llvmFunction);
            lastBlock->addCallee(llvmFunction, function);
            createCallReturn |= true;
        } else if (threads) {
            if (llvmFunction->getName() == "pthread_create") {
                createBlock |= createPthreadCreate(callInst, lastBlock);
            } else if (llvmFunction->getName() == "pthread_join") {
                createCallReturn |= createPthreadJoin(callInst, lastBlock);
            }
        }
    }
}

bool GraphBuilder::createPthreadCreate(const llvm::CallInst *callInst, Block *lastBlock) {
    bool createBlock = false;
    llvm::Value *calledValue = callInst->getArgOperand(2);
    auto forkFunctions = getCalledFunctions(calledValue);
    std::vector <const llvm::Function *> forkFunctionsWithBlock;
    for (auto forkFunction : forkFunctions) {
        if (forkFunction->size() > 0) {
            forkFunctionsWithBlock.push_back(forkFunction);
        }
    }
    createBlock |= forkFunctionsWithBlock.size();
    for (auto forkFunction : forkFunctionsWithBlock) {
        auto function = createOrGetFunction(forkFunction);
        lastBlock->addFork(forkFunction, function);
    }
    return createBlock;
}

bool GraphBuilder::createPthreadJoin(const llvm::CallInst *callInst, Block *lastBlock) {
    bool createCallReturn = false;

    // FIXME: create this as attibute so that we perform the analysis only once
    // (now we do not care as we just forward the results of PTA, but in the future...)
    ForkJoinAnalysis FJA{pointsToAnalysis_};
    auto joinFunctions = FJA.joinFunctions(callInst);
    for (auto joinFunction : joinFunctions) {
        auto llvmFunction = llvm::cast<llvm::Function>(joinFunction);
        if (llvmFunction->size() > 0) {
            auto func = createOrGetFunction(llvmFunction);
            lastBlock->addJoin(llvmFunction, func);
            createCallReturn |= true;
        }
    }
    return createCallReturn;
}

int predecessorsNumber(const llvm::BasicBlock *basicBlock) {
    auto number = std::distance(pred_begin(basicBlock), pred_end(basicBlock));
    return static_cast<int>(number);
}

int successorsNumber(const llvm::BasicBlock *basicBlock) {
    auto number = std::distance(succ_begin(basicBlock), succ_end(basicBlock));
    return static_cast<int>(number);
}

bool isReachable(const llvm::BasicBlock *basicBlock) {
    return predecessorsNumber(basicBlock) > 0 ||
           &basicBlock->getParent()->front() == basicBlock;
}

} // namespace legacy
} // namespace llvmdg
} // namespace dg
