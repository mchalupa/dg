#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/DebugInfoMetadata.h>
SILENCE_LLVM_WARNINGS_POP

#include "dg/tools/llvm-slicer-utils.h"

using MapTy = std::map<const llvm::Value *, CVariableDecl>;

// create the mapping from LLVM values to C variable names
MapTy allocasToVars(const llvm::Function& F) {
    MapTy valuesToVariables;

    for (auto& I : llvm::instructions(F)) {
        llvm::DIVariable *var = nullptr;
        auto loc = I.getDebugLoc();
        if (const llvm::DbgDeclareInst *DD = llvm::dyn_cast<llvm::DbgDeclareInst>(&I)) {
            auto val = DD->getAddress();
            var = DD->getVariable();
            valuesToVariables.emplace(val, CVariableDecl(var->getName().str(),
                                               loc ? loc.getLine() : var->getLine(),
                                               loc ? loc.getCol() : 0));
        } else if (const llvm::DbgValueInst *DV
                    = llvm::dyn_cast<llvm::DbgValueInst>(&I)) {
            auto val = DV->getValue();
            auto var = DV->getVariable();
            valuesToVariables.emplace(val, CVariableDecl(var->getName().str(),
                                               loc ? loc.getLine() : var->getLine(),
                                               loc ? loc.getCol() : 0));
        }
    }

    return valuesToVariables;
}

MapTy allocasToVars(const llvm::Module& M) {
    MapTy valuesToVariables;
    for (auto& F: M) {
        auto tmp = allocasToVars(F);
        valuesToVariables.insert(tmp.begin(), tmp.end());
    }
    return valuesToVariables;
}
