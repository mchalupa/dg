if (LLVM_DG)
#  set(LLVM_LINK_COMPONENTS core engine asmparser bitreader support)

add_library(PSS SHARED
	analysis/Offset.h
	analysis/PointsTo/Pointer.h
	analysis/PointsTo/Pointer.cpp
	analysis/PointsTo/PSS.h
	analysis/PointsTo/PSS.cpp
	analysis/PointsTo/PointsToFlowInsensitive.h
	analysis/PointsTo/PointsToFlowSensitive.h
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
	# -- LLVM
	llvm/LLVMNode.h
	llvm/LLVMNode.cpp
	llvm/LLVMDependenceGraph.h
	llvm/LLVMDependenceGraph.cpp
	llvm/LLVMDGVerifier.h
	llvm/LLVMDGVerifier.cpp
	llvm/Slicer.h
	# -- LLVM analysis
	llvm/analysis/PostDominators.cpp
	llvm/analysis/ReachingDefinitions.h
	llvm/analysis/ReachingDefinitions.cpp
	llvm/analysis/PointsTo.h
	llvm/analysis/PSS.h
	llvm/analysis/PSS.cpp
	llvm/analysis/DefUse.h
	llvm/analysis/DefUse.cpp
	# -- LLVM analysis (old)
	llvm/analysis/old/DefUse.h
	llvm/analysis/old/DefUse.cpp
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
