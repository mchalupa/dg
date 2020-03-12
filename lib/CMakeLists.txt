include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(dganalysis SHARED
	${CMAKE_SOURCE_DIR}/include/dg/Offset.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/DGContainer.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/Bitvector.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/Bits.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/NumberSet.h

	Offset.cpp
)

add_library(dgpta SHARED
	${CMAKE_SOURCE_DIR}/include/dg/SubgraphNode.h
	${CMAKE_SOURCE_DIR}/include/dg/PointerAnalysis/Pointer.h
	${CMAKE_SOURCE_DIR}/include/dg/PointerAnalysis/PointsToSet.h
	${CMAKE_SOURCE_DIR}/include/dg/PointerAnalysis/MemoryObject.h
	${CMAKE_SOURCE_DIR}/include/dg/PointerAnalysis/PointerGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/PointerAnalysis/PointerAnalysis.h
	${CMAKE_SOURCE_DIR}/include/dg/PointerAnalysis/PointerAnalysisFI.h
	${CMAKE_SOURCE_DIR}/include/dg/PointerAnalysis/PointerAnalysisFS.h
	${CMAKE_SOURCE_DIR}/include/dg/PointerAnalysis/PointerGraphValidator.h

	PointerAnalysis/Pointer.cpp
	PointerAnalysis/PointerAnalysis.cpp
	PointerAnalysis/PointerGraphValidator.cpp
	PointerAnalysis/PointsToSet.cpp
)
target_link_libraries(dgpta PUBLIC dganalysis)

add_library(dgdda SHARED
	${CMAKE_SOURCE_DIR}/include/dg/ReachingDefinitions/ReachingDefinitions.h
	${CMAKE_SOURCE_DIR}/include/dg/ReachingDefinitions/RDMap.h
	${CMAKE_SOURCE_DIR}/include/dg/ReadWriteGraph/RWNode.h
	${CMAKE_SOURCE_DIR}/include/dg/ReadWriteGraph/RWBBlock.h
	${CMAKE_SOURCE_DIR}/include/dg/ReadWriteGraph/ReadWriteGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/DataDependence/DataDependenceAnalysisImpl.h
	${CMAKE_SOURCE_DIR}/include/dg/DataDependence/DataDependence.h

	ReachingDefinitions/BasicRDMap.cpp
	ReachingDefinitions/ReachingDefinitions.cpp
	ReadWriteGraph/ReadWriteGraph.cpp
	MemorySSA/MemorySSA.cpp
)
target_link_libraries(dgdda PUBLIC dganalysis)


if (LLVM_DG)

add_library(dgllvmpta SHARED
	${CMAKE_SOURCE_DIR}/include/dg/llvm/PointerAnalysis/PointerAnalysis.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/PointerAnalysis/LLVMPointerAnalysisOptions.h
	${CMAKE_SOURCE_DIR}/include/dg/llvm/PointerAnalysis/PointerGraph.h

	llvm/PointerAnalysis/PointerGraphValidator.h
	llvm/PointerAnalysis/PointerAnalysis.cpp
	llvm/PointerAnalysis/PointerGraph.cpp
	llvm/PointerAnalysis/PointerGraphValidator.cpp
	llvm/PointerAnalysis/Block.cpp
	llvm/PointerAnalysis/Interprocedural.cpp
	llvm/PointerAnalysis/Structure.cpp
	llvm/PointerAnalysis/Globals.cpp
	llvm/PointerAnalysis/Constants.cpp
	llvm/PointerAnalysis/Instructions.cpp
	llvm/PointerAnalysis/Calls.cpp
	llvm/PointerAnalysis/Threads.cpp
)
target_link_libraries(dgllvmpta PUBLIC dgpta)

add_library(dgllvmforkjoin SHARED
	llvm/ForkJoin/ForkJoin.h
	llvm/ForkJoin/ForkJoin.cpp
)

target_link_libraries(dgllvmforkjoin PRIVATE dgllvmpta)

add_library(dgllvmdda SHARED
	llvm/ReadWriteGraph/LLVMReadWriteGraphBuilder.cpp
	llvm/ReadWriteGraph/Instructions.cpp
	llvm/ReadWriteGraph/Calls.cpp
	llvm/DataDependenceAnalysis/LLVMDataDependenceAnalysis.cpp
	llvm/ForkJoin/ForkJoin.h
	llvm/ForkJoin/ForkJoin.cpp

	${CMAKE_SOURCE_DIR}/include/dg/llvm/DataDependence/DataDependence.h
	llvm/ReadWriteGraph/LLVMReadWriteGraphBuilder.h
)
target_link_libraries(dgllvmdda
			PUBLIC dgllvmpta
			PUBLIC dgdda
			PUBLIC dgllvmforkjoin)

add_library(dgllvmthreadregions SHARED "")
include(${CMAKE_CURRENT_SOURCE_DIR}/llvm/ThreadRegions/CMakeLists.txt)
target_include_directories(dgllvmthreadregions
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include/dg/llvm/ThreadRegions)
target_link_libraries(dgllvmthreadregions INTERFACE dgllvmpta PRIVATE dgllvmforkjoin)

add_library(dgllvmcda SHARED "")
include(${CMAKE_CURRENT_SOURCE_DIR}/llvm/ControlDependence/CMakeLists.txt)
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
	llvm/Dominators/PostDominators.cpp
	llvm/DefUse/DefUse.cpp
	llvm/DefUse/DefUse.h
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

add_library(dgsdg SHARED
	SystemDependenceGraph/DependenceGraph.cpp

	#${CMAKE_SOURCE_DIR}/include/dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/SystemDependenceGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/DependenceGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/DGElement.h
	${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/DGNode.h
	${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/DGArgumentPair.h
	${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/DGBBlock.h
)
target_link_libraries(dgsdg
		      PUBLIC dgpta
		      PUBLIC dgdda)

add_library(dgllvmsdg SHARED
	llvm/SystemDependenceGraph/SystemDependenceGraph.cpp
	llvm/SystemDependenceGraph/Dependencies.cpp

	${CMAKE_SOURCE_DIR}/include/dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h
)
target_link_libraries(dgllvmsdg
		      PUBLIC dgsdg
		      PUBLIC dgllvmpta
		      PUBLIC dgllvmdda
		      PUBLIC dgllvmcda)

install(TARGETS dgllvmdg dgllvmthreadregions dgllvmcda
                dgllvmpta dgllvmdda dgpta dgdda dganalysis
		dgllvmforkjoin dgllvmsdg
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

endif(LLVM_DG)
