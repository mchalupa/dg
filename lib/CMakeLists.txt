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
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/PointerGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/PointerAnalysis.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/PointerAnalysisFI.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/PointerAnalysisFS.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/PointsTo/PointerGraphValidator.h

	analysis/PointsTo/Pointer.cpp
	analysis/PointsTo/PointerAnalysis.cpp
	analysis/PointsTo/PointerGraphValidator.cpp
	analysis/PointsTo/PointsToSet.cpp
)
target_link_libraries(PTA PUBLIC DGAnalysis)

add_library(RD SHARED
	${CMAKE_SOURCE_DIR}/include/dg/analysis/ReachingDefinitions/ReachingDefinitions.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/ReachingDefinitions/RDMap.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/ReachingDefinitions/RDNode.h

	analysis/ReachingDefinitions/BasicRDMap.cpp
	analysis/ReachingDefinitions/ReachingDefinitions.cpp
	analysis/ReachingDefinitions/ReachingDefinitionsGraph.cpp
	analysis/MemorySSA/MemorySSA.cpp
)
target_link_libraries(RD PUBLIC DGAnalysis)


if (LLVM_DG)

add_library(LLVMpta SHARED
	${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/PointsTo/PointerAnalysis.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/PointsTo/PointerGraph.h

	llvm/analysis/PointsTo/PointerGraphValidator.h
	llvm/analysis/PointsTo/PointerGraph.cpp
	llvm/analysis/PointsTo/PointerGraphValidator.cpp
	llvm/analysis/PointsTo/Block.cpp
	llvm/analysis/PointsTo/Interprocedural.cpp
	llvm/analysis/PointsTo/Structure.cpp
	llvm/analysis/PointsTo/Globals.cpp
	llvm/analysis/PointsTo/Constants.cpp
	llvm/analysis/PointsTo/Instructions.cpp
	llvm/analysis/PointsTo/Calls.cpp
	llvm/analysis/PointsTo/Threads.cpp
)
target_link_libraries(LLVMpta PUBLIC PTA)

add_library(LLVMForkJoin SHARED
	llvm/analysis/ForkJoin/ForkJoin.h
	llvm/analysis/ForkJoin/ForkJoin.cpp
)

target_link_libraries(LLVMForkJoin PRIVATE LLVMpta)

add_library(LLVMrd SHARED
	llvm/analysis/ReachingDefinitions/LLVMRDBuilder.cpp
	llvm/analysis/ReachingDefinitions/LLVMReachingDefinitions.cpp
	llvm/analysis/ForkJoin/ForkJoin.h
	llvm/analysis/ForkJoin/ForkJoin.cpp

	${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/ReachingDefinitions/ReachingDefinitions.h
	llvm/analysis/ReachingDefinitions/LLVMRDBuilder.h
)
target_link_libraries(LLVMrd
			PUBLIC LLVMpta
			PUBLIC RD
			PUBLIC LLVMForkJoin)

add_library(dgThreadRegions SHARED "")
include(${CMAKE_CURRENT_SOURCE_DIR}/llvm/analysis/ThreadRegions/CMakeLists.txt)
target_include_directories(dgThreadRegions
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/ThreadRegions)
target_link_libraries(dgThreadRegions INTERFACE LLVMpta PRIVATE LLVMForkJoin)

add_library(dgControlDependence SHARED "")
include(${CMAKE_CURRENT_SOURCE_DIR}/llvm/analysis/ControlDependence/CMakeLists.txt)
target_link_libraries(dgControlDependence INTERFACE LLVMpta PRIVATE LLVMForkJoin)

add_library(LLVMdg SHARED
	${CMAKE_SOURCE_DIR}/include/dg/BBlock.h
	${CMAKE_SOURCE_DIR}/include/dg/Node.h
	${CMAKE_SOURCE_DIR}/include/dg/DependenceGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/LLVMNode.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/LLVMDependenceGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/LLVMDependenceGraphBuilder.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/LLVMSlicer.h

	llvm/LLVMDGVerifier.h
	llvm/llvm-utils.h

	llvm/LLVMNode.cpp
	llvm/LLVMDependenceGraph.cpp
	llvm/LLVMDGVerifier.cpp
	llvm/analysis/Dominators/PostDominators.cpp
	llvm/analysis/DefUse/DefUse.cpp
	llvm/analysis/DefUse/DefUse.h
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
				PUBLIC dgThreadRegions
				PUBLIC dgControlDependence
				PRIVATE ${llvm_support}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_bitwriter}
				PRIVATE ${llvm_core})
else()
	target_link_libraries(LLVMdg
				PUBLIC LLVMpta
				PUBLIC LLVMrd
				PUBLIC dgThreadRegions
				PUBLIC dgControlDependence)
endif(APPLE)

install(TARGETS LLVMdg dgThreadRegions dgControlDependence LLVMpta LLVMrd PTA RD DGAnalysis
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

endif(LLVM_DG)
