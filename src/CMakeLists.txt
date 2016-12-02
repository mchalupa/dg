if (LLVM_DG)

add_library(PTA SHARED
	analysis/SubgraphNode.h
	analysis/Offset.h
	analysis/PointsTo/Pointer.h
	analysis/PointsTo/Pointer.cpp
	analysis/PointsTo/PointerSubgraph.h
	analysis/PointsTo/PointerAnalysis.h
	analysis/PointsTo/PointerAnalysis.cpp
	analysis/PointsTo/PointsToFlowInsensitive.h
	analysis/PointsTo/PointsToFlowSensitive.h
)

add_library(RD SHARED
	analysis/SubgraphNode.h
	analysis/Offset.h
	analysis/ReachingDefinitions/ReachingDefinitions.h
	analysis/ReachingDefinitions/ReachingDefinitions.cpp
	analysis/ReachingDefinitions/RDMap.h
	analysis/ReachingDefinitions/RDMap.cpp
)

add_library(LLVMpta SHARED
	llvm/analysis/PointsTo/PointsTo.h
	llvm/analysis/PointsTo/PointerSubgraph.h
	llvm/analysis/PointsTo/PointerSubgraph.cpp
	llvm/analysis/PointsTo/Structure.cpp
	llvm/analysis/PointsTo/Globals.cpp
)

target_link_libraries(LLVMpta PTA)

add_library(LLVMdg SHARED
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
	llvm/llvm-utils.h
	# -- LLVM analysis
	llvm/analysis/PostDominators.cpp
	llvm/analysis/ReachingDefinitions/ReachingDefinitions.h
	llvm/analysis/ReachingDefinitions/ReachingDefinitions.cpp
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

target_link_libraries(LLVMdg LLVMpta RD ${llvm_libs})
install(TARGETS LLVMdg LLVMpta PTA RD
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES
	ADT/Queue.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/ADT/)
install(FILES
	analysis/Offset.h
	analysis/SCC.h
	analysis/SubgraphNode.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/analysis/)
install(FILES
	analysis/PointsTo/PointerAnalysis.h
	analysis/PointsTo/Pointer.h
	analysis/PointsTo/PointerSubgraph.h
	analysis/PointsTo/PointsToFlowInsensitive.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/analysis/PointsTo/)
install(FILES
	llvm/llvm-utils.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/llvm/)
install(FILES
	llvm/analysis/PointsTo/PointerSubgraph.h
	llvm/analysis/PointsTo/PointsTo.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/llvm/analysis/PointsTo/)

endif(LLVM_DG)
