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
	analysis/PointsTo/PointerSubgraphValidator.h
	analysis/PointsTo/PointerSubgraphValidator.cpp
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
	llvm/MemAllocationFuncs.cpp
	llvm/analysis/PointsTo/PointsTo.h
	llvm/analysis/PointsTo/PointerSubgraph.h
	llvm/analysis/PointsTo/PointerSubgraph.cpp
	llvm/analysis/PointsTo/PointerSubgraphValidator.h
	llvm/analysis/PointsTo/PointerSubgraphValidator.cpp
	llvm/analysis/PointsTo/Structure.cpp
	llvm/analysis/PointsTo/Globals.cpp
)

target_link_libraries(LLVMpta PUBLIC PTA)

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
)

target_link_libraries(LLVMdg PUBLIC LLVMpta RD ${llvm_libs})

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
	analysis/PointsTo/PointerSubgraphValidator.h
	analysis/PointsTo/PointsToFlowInsensitive.h
	analysis/PointsTo/PointsToFlowSensitive.h
	analysis/PointsTo/PointsToWithInvalidate.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/analysis/PointsTo/)
install(FILES
	llvm/llvm-utils.h
	llvm/MemAllocationFuncs.h
	llvm/LLVMNode.h
	llvm/LLVMDependenceGraph.h
	llvm/LLVMDGAssemblyAnnotationWriter.h
	llvm/LLVMDGVerifier.h
	llvm/Slicer.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/llvm/)
install(FILES
	llvm/analysis/PointsTo/PointerSubgraph.h
	llvm/analysis/PointsTo/PointerSubgraphValidator.h
	llvm/analysis/PointsTo/PointsTo.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/llvm/analysis/PointsTo/)

endif(LLVM_DG)
