#ifndef _DG_TOOLS_LLVM_SLICER_OPTS_H_
#define  _DG_TOOLS_LLVM_SLICER_OPTS_H_

#include <vector>
#include <llvm/Support/CommandLine.h>

#include "dg/llvm/LLVMDependenceGraphBuilder.h"

// CommandLine Category for slicer options
extern llvm::cl::OptionCategory SlicingOpts;

// Object representing options for slicer
struct SlicerOptions {
    dg::llvmdg::LLVMDependenceGraphOptions dgOptions{};

    // FIXME: get rid of this once we got the secondary SC
    std::vector<std::string> additionalSlicingCriteria{};

    // bodies of these functions will not be sliced
    std::vector<std::string> preservedFunctions{};

    // slice away also the slicing criteria nodes
    // (if they are not dependent on themselves)
    bool removeSlicingCriteria{false};

    // do we perform forward slicing?
    bool forwardSlicing{false};

    std::string slicingCriteria{};
    std::string secondarySlicingCriteria{};
    std::string inputFile{};
    std::string outputFile{};
};

///
// Return filled SlicerOptions structure.
SlicerOptions parseSlicerOptions(int argc, char *argv[], bool requireCrit = false);

#endif  // _DG_TOOLS_LLVM_SLICER_OPTS_H_

