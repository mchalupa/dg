#ifndef DG_TOOLS_LLVM_SLICER_OPTS_H_
#define DG_TOOLS_LLVM_SLICER_OPTS_H_

#include <vector>
#include <set>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Support/CommandLine.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

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

    // string describing the slicing criteria
    std::string slicingCriteria{};
    // SC string in the old format
    std::string legacySlicingCriteria{};
    // legacy secondary SC
    std::string legacySecondarySlicingCriteria{};

    std::string inputFile{};
    std::string outputFile{};
};

///
// Return filled SlicerOptions structure.
SlicerOptions
parseSlicerOptions(int argc, char *argv[],
                   bool requireCrit = false,
                   bool inputFileRequired = true);

bool getSlicingCriteriaNodes(dg::LLVMDependenceGraph& dg,
                             const std::string& slicingCriteria,
                             const std::string& legacySlicingCriteria,
                             const std::string& legacySecondarySlicingCriteria,
                             std::set<dg::LLVMNode *>& criteria_nodes,
                             bool criteria_are_next_instr = false);

#endif  // DG_TOOLS_LLVM_SLICER_OPTS_H_

