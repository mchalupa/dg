include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(dganalysis SHARED
	${CMAKE_SOURCE_DIR}/include/dg/analysis/Offset.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/DGContainer.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/Bitvector.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/Bits.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/NumberSet.h

	analysis/Offset.cpp
)

add_library(dgpta SHARED
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
target_link_libraries(dgpta PUBLIC dganalysis)

add_library(dgdda SHARED
	${CMAKE_SOURCE_DIR}/include/dg/analysis/ReachingDefinitions/ReachingDefinitions.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/ReachingDefinitions/RDMap.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/ReadWriteGraph/RWNode.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/ReadWriteGraph/RWBBlock.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/ReadWriteGraph/ReadWriteGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/DataDependence/DataDependenceAnalysisImpl.h
	${CMAKE_SOURCE_DIR}/include/dg/analysis/DataDependence/DataDependence.h

	analysis/ReachingDefinitions/BasicRDMap.cpp
	analysis/ReachingDefinitions/ReachingDefinitions.cpp
	analysis/ReadWriteGraph/ReadWriteGraph.cpp
	analysis/MemorySSA/MemorySSA.cpp
)
target_link_libraries(dgdda PUBLIC dganalysis)


if (LLVM_DG)

add_library(dgllvmpta SHARED
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
target_link_libraries(dgllvmpta PUBLIC dgpta)

add_library(dgllvmforkjoin SHARED
	llvm/analysis/ForkJoin/ForkJoin.h
	llvm/analysis/ForkJoin/ForkJoin.cpp
)

target_link_libraries(dgllvmforkjoin PRIVATE dgllvmpta)

add_library(dgllvmdda SHARED
	llvm/analysis/ReadWriteGraph/LLVMReadWriteGraphBuilder.cpp
	llvm/analysis/DataDependenceAnalysis/LLVMDataDependenceAnalysis.cpp
	llvm/analysis/ForkJoin/ForkJoin.h
	llvm/analysis/ForkJoin/ForkJoin.cpp

	${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/DataDependence/DataDependence.h
	llvm/analysis/ReadWriteGraph/LLVMReadWriteGraphBuilder.h
)
target_link_libraries(dgllvmdda
			PUBLIC dgllvmpta
			PUBLIC dgdda
			PUBLIC dgllvmforkjoin)

add_library(dgllvmthreadregions SHARED "")
include(${CMAKE_CURRENT_SOURCE_DIR}/llvm/analysis/ThreadRegions/CMakeLists.txt)
target_include_directories(dgllvmthreadregions
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include/dg/llvm/analysis/ThreadRegions)
target_link_libraries(dgllvmthreadregions INTERFACE dgllvmpta PRIVATE dgllvmforkjoin)

add_library(dgllvmcda SHARED "")
include(${CMAKE_CURRENT_SOURCE_DIR}/llvm/analysis/ControlDependence/CMakeLists.txt)
target_link_libraries(dgllvmcda INTERFACE dgllvmpta PRIVATE dgllvmforkjoin)

add_library(dgllvmdg SHARED
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
  set_target_properties(dgllvmdg PROPERTIES
    LINK_FLAGS "-undefined dynamic_lookup"
  )

  set_target_properties(dgllvmpta PROPERTIES
    LINK_FLAGS "-undefined dynamic_lookup"
  )

  set_target_properties(dgllvmdda PROPERTIES
    LINK_FLAGS "-undefined dynamic_lookup"
  )
endif(APPLE)

if (APPLE)
	target_link_libraries(dgllvmdg
				PUBLIC dgllvmpta
				PUBLIC dgllvmdda
				PUBLIC dgllvmthreadregions
				PUBLIC dgllvmcda
				PRIVATE ${llvm_support}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_bitwriter}
				PRIVATE ${llvm_core})
else()
	target_link_libraries(dgllvmdg
				PUBLIC dgllvmpta
				PUBLIC dgllvmdda
				PUBLIC dgllvmthreadregions
				PUBLIC dgllvmcda)
endif(APPLE)

install(TARGETS dgllvmdg dgllvmthreadregions dgllvmcda dgllvmpta dgllvmdda dgpta dgdda dganalysis
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

endif(LLVM_DG)
