include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(DGAnalysis SHARED
	${CMAKE_SOURCE_DIR}/include/dg/analysis/Offset.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/DGContainer.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/Bitvector.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/Bits.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/NumberSet.h

	analysis/Offset.cpp
)

add_library(PTA SHARED
	${CMAKE_SOURCE_DIR}/include/dg/analysis/SubgraphNode.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/Pointer.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/PointsToSet.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/MemoryObject.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/PointerSubgraph.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/PointerAnalysis.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/PointerAnalysisFI.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/PointerAnalysisFS.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/PointerSubgraphValidator.h

	analysis/PointsTo/Pointer.cpp
	analysis/PointsTo/PointerAnalysis.cpp
	analysis/PointsTo/PointerSubgraphValidator.cpp
)
target_link_libraries(PTA PUBLIC DGAnalysis)

add_library(RD SHARED
	${CMAKE_SOURCE_DIR}/include/dg/analysis/ReachingDefinitions/ReachingDefinitions.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/ReachingDefinitions/RDMap.h

	analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFI.h
	analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFS.h

	analysis/ReachingDefinitions/BasicRDMap.cpp
	analysis/ReachingDefinitions/ReachingDefinitions.cpp
	analysis/ReachingDefinitions/Srg/SemisparseRda.cpp
	analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFI.cpp
	analysis/ReachingDefinitions/Srg/MarkerSRGBuilderFS.cpp
)
target_link_libraries(RD PUBLIC DGAnalysis)


if (LLVM_DG)

add_library(LLVMpta SHARED
	${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/PointsTo/PointerAnalysis.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/PointsTo/PointerSubgraph.h

	llvm/analysis/PointsTo/PointerSubgraphValidator.h
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
	llvm/analysis/ReachingDefinitions/LLVMRDBuilderSemisparse.cpp
	llvm/analysis/ReachingDefinitions/LLVMRDBuilderDense.cpp
	llvm/analysis/ReachingDefinitions/LLVMReachingDefinitions.cpp

	${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/ReachingDefinitions/ReachingDefinitions.h
	llvm/analysis/ReachingDefinitions/LLVMRDBuilder.h
	llvm/analysis/ReachingDefinitions/LLVMRDBuilderSemisparse.h
	llvm/analysis/ReachingDefinitions/LLVMRDBuilderDense.h
)
target_link_libraries(LLVMrd
			PUBLIC LLVMpta
			PUBLIC RD)

add_library(ThreadRegions SHARED "")
include(${CMAKE_CURRENT_SOURCE_DIR}/llvm/analysis/ThreadRegions/CMakeLists.txt)
target_include_directories(ThreadRegions
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/ThreadRegions)
target_link_libraries(ThreadRegions
    INTERFACE LLVMpta)

add_library(ControlDependence SHARED "")
include(${CMAKE_CURRENT_SOURCE_DIR}/llvm/analysis/ControlDependence/CMakeLists.txt)
target_link_libraries(ControlDependence
    INTERFACE LLVMpta)

add_library(LLVMdg SHARED
	${CMAKE_SOURCE_DIR}/include/dg/BBlock.h
	${CMAKE_SOURCE_DIR}/include/dg/Node.h
	${CMAKE_SOURCE_DIR}/include/dg/DependenceGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/LLVMNode.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/LLVMDependenceGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/LLVMDependenceGraphBuilder.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/LLVMSlicer.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/DefUse/DefUse.h

	llvm/LLVMDGVerifier.h
	llvm/llvm-utils.h

	llvm/LLVMNode.cpp
	llvm/LLVMDependenceGraph.cpp
	llvm/LLVMDGVerifier.cpp
	llvm/analysis/Dominators/PostDominators.cpp
	llvm/analysis/DefUse/DefUse.cpp
)

# Get proper shared-library behavior (where symbols are not necessarily
# resolved when the shared library is linked) on OS X.
if(APPLE)
  set_target_properties(LLVMdg PROPERTIES
    LINK_FLAGS "-undefined dynamic_lookup"
  )

  set_target_properties(LLVMpta PROPERTIES
    LINK_FLAGS "-undefined dynamic_lookup"
  )

  set_target_properties(LLVMrd PROPERTIES
    LINK_FLAGS "-undefined dynamic_lookup"
  )
endif(APPLE)

if (APPLE)
	target_link_libraries(LLVMdg
				PUBLIC LLVMpta
				PUBLIC LLVMrd
				PUBLIC ThreadRegions
                                PUBLIC ControlDependence
				PRIVATE ${llvm_support}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_bitwriter}
				PRIVATE ${llvm_core})
else()
	target_link_libraries(LLVMdg
				PUBLIC LLVMpta
				PUBLIC LLVMrd
                                PUBLIC ThreadRegions
                                PUBLIC ControlDependence)
endif(APPLE)

install(TARGETS LLVMdg ThreadRegions LLVMpta LLVMrd PTA RD DGAnalysis
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

endif(LLVM_DG)
