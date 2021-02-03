include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(dganalysis SHARED
	${CMAKE_SOURCE_DIR}/include/dg/Offset.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/DGContainer.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/Bitvector.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/Bits.h
	${CMAKE_SOURCE_DIR}/include/dg/ADT/NumberSet.h

	Offset.cpp
        Debug.cpp
        BBlockBase.cpp
)

add_library(dgpta SHARED
	${CMAKE_SOURCE_DIR}/include/dg/SubgraphNode.h
    ${CMAKE_SOURCE_DIR}/include/dg/PointerAnalysis/PointsToSets/LookupTable.h
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
	PointerAnalysis/PointerGraph.cpp
	PointerAnalysis/PointerGraphOptimizations.cpp
	PointerAnalysis/PointerGraphValidator.cpp
	PointerAnalysis/PointsToSet.cpp
)
target_link_libraries(dgpta PUBLIC dganalysis)

add_library(dgdda SHARED
	${CMAKE_SOURCE_DIR}/include/dg/ReadWriteGraph/RWNode.h
	${CMAKE_SOURCE_DIR}/include/dg/ReadWriteGraph/RWBBlock.h
	${CMAKE_SOURCE_DIR}/include/dg/ReadWriteGraph/ReadWriteGraph.h
	${CMAKE_SOURCE_DIR}/include/dg/DataDependence/DataDependenceAnalysisImpl.h
	${CMAKE_SOURCE_DIR}/include/dg/DataDependence/DataDependence.h
	${CMAKE_SOURCE_DIR}/include/dg/MemorySSA/MemorySSA.h
	${CMAKE_SOURCE_DIR}/include/dg/MemorySSA/ModRef.h
	${CMAKE_SOURCE_DIR}/include/dg/MemorySSA/Definitions.h

	ReadWriteGraph/ReadWriteGraph.cpp
	MemorySSA/MemorySSA.cpp
        MemorySSA/ModRef.cpp
        MemorySSA/Definitions.cpp
)
target_link_libraries(dgdda PUBLIC dganalysis)

add_library(dgcda SHARED
	${CMAKE_SOURCE_DIR}/include/dg/ControlDependence/ControlDependence.h
	${CMAKE_SOURCE_DIR}/include/dg/ControlDependence/ControlDependenceAnalysisOptions.h
        ControlDependence/CDGraph.h
        ControlDependence/NTSCD.h
        ControlDependence/DOD.h
        ControlDependence/ControlClosure.h
        ControlDependence/NTSCD.cpp
)

add_library(dgsdg SHARED
    SystemDependenceGraph/DependenceGraph.cpp

    #${CMAKE_SOURCE_DIR}/include/dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h
    ${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/SystemDependenceGraph.h
    ${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/DependenceGraph.h
    ${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/DGElement.h
    ${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/DepDGElement.h
    ${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/DGNode.h
    ${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/DGArgumentPair.h
    ${CMAKE_SOURCE_DIR}/include/dg/SystemDependenceGraph/DGBBlock.h
)
target_link_libraries(dgsdg PUBLIC dgpta
                            PUBLIC dgdda)

install(TARGETS dganalysis dgcda dgpta dgdda dgsdg
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

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
target_link_libraries(dgllvmpta PUBLIC dgpta
                                PUBLIC ${llvm}) # only for shared LLVM

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

add_library(dgllvmthreadregions SHARED
            llvm/ThreadRegions/Nodes/Node.cpp
            llvm/ThreadRegions/Nodes/GeneralNode.cpp
            llvm/ThreadRegions/Nodes/ForkNode.cpp
            llvm/ThreadRegions/Nodes/JoinNode.cpp
            llvm/ThreadRegions/Nodes/LockNode.cpp
            llvm/ThreadRegions/Nodes/UnlockNode.cpp
            llvm/ThreadRegions/Nodes/EntryNode.cpp
            llvm/ThreadRegions/Nodes/ExitNode.cpp
            llvm/ThreadRegions/Nodes/CallNode.cpp
            llvm/ThreadRegions/Nodes/CallFuncPtrNode.cpp
            llvm/ThreadRegions/Nodes/CallReturnNode.cpp
            llvm/ThreadRegions/Nodes/ReturnNode.cpp
            llvm/ThreadRegions/Nodes/NodeIterator.cpp
            llvm/ThreadRegions/Graphs/BlockGraph.cpp
            llvm/ThreadRegions/Graphs/FunctionGraph.cpp
            llvm/ThreadRegions/Graphs/GraphBuilder.cpp
            llvm/ThreadRegions/Graphs/ThreadRegionsBuilder.cpp
            llvm/ThreadRegions/Graphs/CriticalSectionsBuilder.cpp
            llvm/ThreadRegions/Graphs/ControlFlowGraph.cpp
	    llvm/ThreadRegions/ThreadRegion.cpp
            llvm/ThreadRegions/MayHappenInParallel.cpp)
target_include_directories(dgllvmthreadregions
        PUBLIC ${CMAKE_SOURCE_DIR}/include/dg/llvm/ThreadRegions)
target_link_libraries(dgllvmthreadregions PUBLIC dgllvmpta
                                          PRIVATE dgllvmforkjoin)

add_library(dgllvmcda SHARED
            llvm/ControlDependence/legacy/Block.cpp
            llvm/ControlDependence/legacy/Function.cpp
            llvm/ControlDependence/legacy/GraphBuilder.cpp
            llvm/ControlDependence/legacy/NTSCD.cpp
            llvm/ControlDependence/legacy/NTSCD.h
            llvm/ControlDependence/ControlDependence.cpp
            llvm/ControlDependence/InterproceduralCD.cpp
            llvm/ControlDependence/InterproceduralCD.h
            llvm/ControlDependence/SCD.cpp
            llvm/ControlDependence/SCD.h
            llvm/ControlDependence/NTSCD.h
            llvm/ControlDependence/ControlClosure.h
            llvm/ControlDependence/GraphBuilder.h
            ${CMAKE_SOURCE_DIR}/include/dg/llvm/ControlDependence/ControlDependence.h
            ${CMAKE_SOURCE_DIR}/include/dg/llvm/ControlDependence/LLVMControlDependenceAnalysisImpl.h
            )
target_link_libraries(dgllvmcda PUBLIC dgllvmpta
                                PUBLIC dgcda
                                INTERFACE ${llvm_analysis} # only for static LLVM
                                PRIVATE dgllvmforkjoin)

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
target_link_libraries(dgllvmdg
			PUBLIC dgllvmpta
			PUBLIC dgllvmdda
			PUBLIC dgllvmthreadregions
			PUBLIC dgllvmcda)

add_library(dgllvmsdg SHARED
	llvm/SystemDependenceGraph/SystemDependenceGraph.cpp
	llvm/SystemDependenceGraph/Dependencies.cpp

	${CMAKE_SOURCE_DIR}/include/dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h
)
target_link_libraries(dgllvmsdg PUBLIC dgsdg
                                PRIVATE dgllvmdda)

install(TARGETS dgllvmdg dgllvmthreadregions dgllvmcda
                dgllvmpta dgllvmdda dgllvmforkjoin dgllvmsdg
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

endif(LLVM_DG)
