add_library(dganalysis SHARED
	Offset.cpp
        Debug.cpp
        BBlockBase.cpp
)

add_library(dgpta SHARED
	PointerAnalysis/Pointer.cpp
	PointerAnalysis/PointerAnalysis.cpp
	PointerAnalysis/PointerGraph.cpp
	PointerAnalysis/PointerGraphOptimizations.cpp
	PointerAnalysis/PointerGraphValidator.cpp
	PointerAnalysis/PointsToSet.cpp
)
target_link_libraries(dgpta PUBLIC dganalysis)

add_library(dgdda SHARED
	ReadWriteGraph/ReadWriteGraph.cpp
	MemorySSA/MemorySSA.cpp
        MemorySSA/ModRef.cpp
        MemorySSA/Definitions.cpp
)
target_link_libraries(dgdda PUBLIC dganalysis)

add_library(dgcda SHARED
        ControlDependence/NTSCD.cpp
)

add_library(dgsdg SHARED
    SystemDependenceGraph/DependenceGraph.cpp
)
target_link_libraries(dgsdg PUBLIC dgpta
                            PUBLIC dgdda)

add_library(dgvra SHARED
	ValueRelations/Relations.cpp
)

install(TARGETS dganalysis dgcda dgpta dgdda dgsdg dgvra
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if (LLVM_DG)

add_library(dgllvmpta SHARED
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
	llvm/ForkJoin/ForkJoin.cpp
)
target_link_libraries(dgllvmforkjoin PRIVATE dgllvmpta)

add_library(dgllvmdda SHARED
	llvm/ReadWriteGraph/LLVMReadWriteGraphBuilder.cpp
	llvm/ReadWriteGraph/Instructions.cpp
	llvm/ReadWriteGraph/Calls.cpp
	llvm/DataDependenceAnalysis/LLVMDataDependenceAnalysis.cpp
	llvm/ForkJoin/ForkJoin.cpp
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
            llvm/ThreadRegions/MayHappenInParallel.cpp
)
target_link_libraries(dgllvmthreadregions PUBLIC dgllvmpta
                                          PRIVATE dgllvmforkjoin)

add_library(dgllvmcda SHARED
            llvm/ControlDependence/legacy/Block.cpp
            llvm/ControlDependence/legacy/Function.cpp
            llvm/ControlDependence/legacy/GraphBuilder.cpp
            llvm/ControlDependence/legacy/NTSCD.cpp
            llvm/ControlDependence/ControlDependence.cpp
            llvm/ControlDependence/InterproceduralCD.cpp
            llvm/ControlDependence/SCD.cpp
)
target_link_libraries(dgllvmcda PUBLIC dgllvmpta
                                PUBLIC dgcda
                                PRIVATE dgllvmforkjoin)

add_library(dgllvmdg SHARED
	llvm/LLVMNode.cpp
	llvm/LLVMDependenceGraph.cpp
	llvm/LLVMDGVerifier.cpp
	llvm/Dominators/PostDominators.cpp
	llvm/DefUse/DefUse.cpp
)

target_link_libraries(dgllvmdg
			PUBLIC dgllvmpta
			PUBLIC dgllvmdda
			PUBLIC dgllvmthreadregions
			PUBLIC dgllvmcda
			INTERFACE ${llvm_analysis}) # only for static LLVM

add_library(dgllvmvra SHARED
	llvm/ValueRelations/GraphBuilder.cpp
	llvm/ValueRelations/GraphElements.cpp
	llvm/ValueRelations/RelationsAnalyzer.cpp
	llvm/ValueRelations/StructureAnalyzer.cpp
	llvm/ValueRelations/ValueRelations.cpp
)
target_link_libraries(dgllvmvra PUBLIC dgvra
                                PRIVATE dganalysis
								PUBLIC ${llvm}) # only for shared LLVM

add_library(dgllvmsdg SHARED
	llvm/SystemDependenceGraph/SystemDependenceGraph.cpp
	llvm/SystemDependenceGraph/Dependencies.cpp
)
target_link_libraries(dgllvmsdg PUBLIC dgsdg
                                PRIVATE dgllvmdda)

install(TARGETS dgllvmdg dgllvmthreadregions dgllvmcda
                dgllvmpta dgllvmdda dgllvmforkjoin dgllvmsdg dgllvmvra
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

endif(LLVM_DG)
