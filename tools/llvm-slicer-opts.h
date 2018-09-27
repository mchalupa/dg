#ifndef _DG_TOOLS_LLVM_SLICER_OPTS_H_
#define  _DG_TOOLS_LLVM_SLICER_OPTS_H_

#include "llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h"
#include "llvm/analysis/ReachingDefinitions/LLVMReachingDefinitionsAnalysisOptions.h"

extern llvm::cl::OptionCategory SlicingOpts;
extern std::string output;
extern std::string llvmfile;
extern std::string slicing_criteria;
extern bool remove_slicing_criteria;
extern uint64_t pta_field_sensitivie;
extern bool rd_strong_update_unknown;
extern bool undefined_are_pure;
extern std::string entry_func;
extern bool forward_slice;
extern dg::analysis::LLVMPointerAnalysisOptions::AnalysisType ptaType;
extern dg::analysis::LLVMReachingDefinitionsAnalysisOptions::AnalysisType rdaType;
extern dg::CD_ALG cdAlgorithm;

#endif  // _DG_TOOLS_LLVM_SLICER_OPTS_H_

