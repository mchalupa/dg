if (LLVM_DG)
#  set(LLVM_LINK_COMPONENTS core engine asmparser bitreader support)

add_library(PSS SHARED
	analysis/Offset.h
	analysis/Pointer.h
	analysis/Pointer.cpp
	analysis/PSS.h
	analysis/PSS.cpp
	analysis/PointsToFlowInsensitive.h
	analysis/PointsToFlowSensitive.h
)

add_library(RD SHARED
	analysis/Offset.h
	analysis/ReachingDefinitions.h
	analysis/ReachingDefinitions.cpp
	analysis/RDMap.h
	analysis/RDMap.cpp
)

add_library(LLVMdg SHARED
	Utils.h
	BBlock.h
	Node.h
	DependenceGraph.h
	ADT/DGContainer.h
	llvm/LLVMNode.h
	llvm/LLVMNode.cpp
	llvm/LLVMDependenceGraph.h
	llvm/LLVMDependenceGraph.cpp
	llvm/LLVMDGVerifier.h
	llvm/LLVMDGVerifier.cpp
	llvm/PostDominators.cpp
	llvm/DefUse.h
	llvm/DefUse.cpp
	llvm/Slicer.h
	# LLVM analysis
	llvm/analysis/ReachingDefinitions.h
	llvm/analysis/ReachingDefinitions.cpp
	llvm/analysis/PointsTo.h
	llvm/analysis/PSS.h
	llvm/analysis/PSS.cpp
	# LLVM analysis (old)
	llvm/analysis/old/AnalysisGeneric.h
	llvm/analysis/old/AnalysisGeneric.cpp
	llvm/analysis/old/PointsTo.h
	llvm/analysis/old/PointsTo.cpp
	llvm/analysis/old/ReachingDefs.h
	llvm/analysis/old/ReachingDefs.cpp
	llvm/analysis/old/DefMap.h
	llvm/analysis/old/DefMap.cpp
)

target_link_libraries(LLVMdg PSS RD ${llvm_libs})
install(TARGETS LLVMdg PSS RD
	LIBRARY DESTINATION lib)

endif(LLVM_DG)
