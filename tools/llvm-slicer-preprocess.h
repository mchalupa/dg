#ifndef LLVM_SLICER_PREPROCESS_H_
#define LLVM_SLICER_PREPROCESS_H_

#include <vector>

namespace llvm {
    class Module;
    class Instruction;
}

namespace dg {
namespace llvmdg {

bool cutoffDivergingBranches(llvm::Module& M,
                             const std::string& entry,
                             const std::vector<const llvm::Value *>& criteria);
} // namespace llvmdg
} // namespace dg

#endif
