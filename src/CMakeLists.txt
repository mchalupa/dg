if (LLVM_DG)

add_library(PTA SHARED
	analysis/SubgraphNode.h
	analysis/Offset.h
	analysis/Offset.cpp
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
	analysis/Offset.cpp
	analysis/ReachingDefinitions/ReachingDefinitions.h
	analysis/ReachingDefinitions/ReachingDefinitions.cpp
	analysis/ReachingDefinitions/RDMap.h
	analysis/ReachingDefinitions/RDMap.cpp
	analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFI.h
	analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFI.cpp
	analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFS.h
	analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFS.cpp
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
	llvm/analysis/PointsTo/Constants.cpp
	llvm/analysis/PointsTo/Instructions.cpp
	llvm/analysis/PointsTo/Calls.cpp
)

target_link_libraries(LLVMpta PUBLIC PTA)

add_library(LLVMrd SHARED
	llvm/analysis/ReachingDefinitions/ReachingDefinitions.h
	llvm/analysis/ReachingDefinitions/LLVMRDBuilder.h
	llvm/analysis/ReachingDefinitions/LLVMRDBuilderSemisparse.h
	llvm/analysis/ReachingDefinitions/LLVMRDBuilderSemisparse.cpp
	llvm/analysis/ReachingDefinitions/LLVMRDBuilderDense.h
	llvm/analysis/ReachingDefinitions/LLVMRDBuilderDense.cpp
)
target_link_libraries(LLVMrd
			PUBLIC LLVMpta
			PUBLIC RD)

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
	llvm/analysis/DefUse.h
	llvm/analysis/DefUse.cpp
)

target_link_libraries(LLVMdg
			PUBLIC LLVMpta
			PUBLIC LLVMrd)

install(TARGETS LLVMdg LLVMpta LLVMrd PTA RD
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES
	ADT/Queue.h
        ADT/DGContainer.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/ADT/)
install(FILES
	analysis/Offset.h
	analysis/SCC.h
	analysis/SubgraphNode.h
        analysis/Analysis.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/analysis/)
install(FILES
        analysis/ControlExpression/ControlExpression.h
        analysis/ControlExpression/CENode.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/analysis/ControlExpression/)
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
install(FILES
        Node.h
        DGParameters.h
        BBlock.h
        DependenceGraph.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/)

endif(LLVM_DG)
