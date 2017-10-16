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
	analysis/ReachingDefinitions/Srg/MarkerSRGBuilder.h
	analysis/ReachingDefinitions/Srg/MarkerSRGBuilder.cpp
)

add_library(LLVMpta SHARED
    llvm/MemAllocationFuncs.cpp
	llvm/analysis/PointsTo/PointsTo.h
	llvm/analysis/PointsTo/PointerSubgraph.h
	llvm/analysis/PointsTo/PointerSubgraph.cpp
	llvm/analysis/PointsTo/Structure.cpp
	llvm/analysis/PointsTo/Globals.cpp
)

target_link_libraries(LLVMpta PUBLIC PTA)

# We need all the symbols with dynamic libs. With static libs, we get errors.
if (BUILD_SHARED_LIBS)
	if (${LLVM_PACKAGE_VERSION} VERSION_GREATER "3.4")
		llvm_map_components_to_libnames(LP_llvm_libs core support)
	else()
		llvm_map_components_to_libraries(LP_llvm_libs core support)
	endif()

	target_link_libraries(LLVMpta PUBLIC ${LP_llvm_libs})
endif()

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
	analysis/PointsTo/PointsToFlowInsensitive.h
	analysis/PointsTo/PointsToFlowSensitive.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/analysis/PointsTo/)
install(FILES
	llvm/llvm-utils.h
	llvm/MemAllocationFuncs.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/llvm/)
install(FILES
	llvm/analysis/PointsTo/PointerSubgraph.h
	llvm/analysis/PointsTo/PointsTo.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvm-dg/llvm/analysis/PointsTo/)

endif(LLVM_DG)
