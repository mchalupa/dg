include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(DGAnalysis SHARED
	${CMAKE_SOURCE_DIR}/include/analysis/Offset.h
	${CMAKE_SOURCE_DIR}/include/ADT/DGContainer.h
	${CMAKE_SOURCE_DIR}/include/ADT/Bitvector.h
	${CMAKE_SOURCE_DIR}/include/ADT/Bits.h
	${CMAKE_SOURCE_DIR}/include/ADT/NumberSet.h

	analysis/Offset.cpp
)

add_library(PTA SHARED
	${CMAKE_SOURCE_DIR}/include/analysis/SubgraphNode.h
	${CMAKE_SOURCE_DIR}/include/analysis/PointsTo/Pointer.h
	${CMAKE_SOURCE_DIR}/include/analysis/PointsTo/PointsToSet.h
	${CMAKE_SOURCE_DIR}/include/analysis/PointsTo/MemoryObject.h
	${CMAKE_SOURCE_DIR}/include/analysis/PointsTo/PointerSubgraph.h
	${CMAKE_SOURCE_DIR}/include/analysis/PointsTo/PointerAnalysis.h
	${CMAKE_SOURCE_DIR}/include/analysis/PointsTo/PointsToFlowInsensitive.h
	${CMAKE_SOURCE_DIR}/include/analysis/PointsTo/PointsToFlowSensitive.h
	${CMAKE_SOURCE_DIR}/include/analysis/PointsTo/PointerSubgraphValidator.h

	analysis/PointsTo/Pointer.cpp
	analysis/PointsTo/PointerAnalysis.cpp
	analysis/PointsTo/PointerSubgraphValidator.cpp
)
target_link_libraries(PTA PUBLIC DGAnalysis)

add_library(RD SHARED
	${CMAKE_SOURCE_DIR}/include/analysis/ReachingDefinitions/ReachingDefinitions.h
	${CMAKE_SOURCE_DIR}/include/analysis/ReachingDefinitions/RDMap.h

	${CMAKE_SOURCE_DIR}/include/analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFI.h
	${CMAKE_SOURCE_DIR}/include/analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFS.h

	analysis/ReachingDefinitions/RDMap.cpp
	analysis/ReachingDefinitions/ReachingDefinitions.cpp
	analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFI.cpp
	analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFS.cpp
)
target_link_libraries(RD PUBLIC DGAnalysis)


if (LLVM_DG)

add_library(LLVMpta SHARED
	${CMAKE_SOURCE_DIR}/include/llvm/analysis/PointsTo/PointerAnalysis.h
	${CMAKE_SOURCE_DIR}/include/llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h
	${CMAKE_SOURCE_DIR}/include/llvm/analysis/PointsTo/PointerSubgraphValidator.h
	${CMAKE_SOURCE_DIR}/include/llvm/analysis/PointsTo/PointerSubgraph.h

	llvm/MemAllocationFuncs.h
	llvm/analysis/PointsTo/PointerSubgraph.cpp
	llvm/analysis/PointsTo/PointerSubgraphValidator.cpp
	llvm/analysis/PointsTo/Structure.cpp
	llvm/analysis/PointsTo/Globals.cpp
	llvm/analysis/PointsTo/Constants.cpp
	llvm/analysis/PointsTo/Instructions.cpp
	llvm/analysis/PointsTo/Calls.cpp
)
target_link_libraries(LLVMpta PUBLIC PTA)

add_library(LLVMrd SHARED
	llvm/MemAllocationFuncs.h
	llvm/analysis/ReachingDefinitions/LLVMRDBuilderSemisparse.cpp
	llvm/analysis/ReachingDefinitions/LLVMRDBuilderDense.cpp

	${CMAKE_SOURCE_DIR}/include/llvm/analysis/ReachingDefinitions/ReachingDefinitions.h
	${CMAKE_SOURCE_DIR}/include/llvm/analysis/ReachingDefinitions/LLVMRDBuilder.h
	${CMAKE_SOURCE_DIR}/include/llvm/analysis/ReachingDefinitions/LLVMRDBuilderSemisparse.h
	${CMAKE_SOURCE_DIR}/include/llvm/analysis/ReachingDefinitions/LLVMRDBuilderDense.h
)
target_link_libraries(LLVMrd
			PUBLIC LLVMpta
			PUBLIC RD)

add_library(LLVMdg SHARED
	${CMAKE_SOURCE_DIR}/include/BBlock.h
	${CMAKE_SOURCE_DIR}/include/Node.h
	${CMAKE_SOURCE_DIR}/include/DependenceGraph.h
	${CMAKE_SOURCE_DIR}/include/llvm/LLVMNode.h
	${CMAKE_SOURCE_DIR}/include/llvm/LLVMDependenceGraph.h
	${CMAKE_SOURCE_DIR}/include/llvm/LLVMDependenceGraphBuilder.h
	${CMAKE_SOURCE_DIR}/include/llvm/LLVMDGVerifier.h
	${CMAKE_SOURCE_DIR}/include/llvm/LLVMSlicer.h
	${CMAKE_SOURCE_DIR}/include/llvm/analysis/DefUse/DefUse.h

	llvm/llvm-utils.h

	llvm/LLVMNode.cpp
	llvm/LLVMDependenceGraph.cpp
	llvm/LLVMDGVerifier.cpp
	llvm/analysis/Dominators/PostDominators.cpp
	llvm/analysis/DefUse/DefUse.cpp
)

target_link_libraries(LLVMdg
			PUBLIC LLVMpta
			PUBLIC LLVMrd)

install(TARGETS LLVMdg LLVMpta LLVMrd PTA RD DGAnalysis
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

endif(LLVM_DG)
