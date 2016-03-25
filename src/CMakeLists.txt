if (LLVM_DG)
  set(LLVM_LINK_COMPONENTS core engine asmparser bitreader support)

add_library(PSS SHARED
	analysis/Offset.h
	analysis/Pointer.h
	analysis/Pointer.cpp
	analysis/PSS.h
	analysis/PSS.cpp
	analysis/PointsToFlowInsensitive.h
	analysis/PointsToFlowSensitive.h
)

add_library(RD SHARED
	analysis/Offset.h
	analysis/ReachingDefinitions.h
	analysis/ReachingDefinitions.cpp
	analysis/RDMap.h
	analysis/RDMap.cpp
)

add_library(LLVMdg SHARED
	Utils.h
	BBlock.h
	Node.h
	DependenceGraph.h
	ADT/DGContainer.h
	llvm/LLVMNode.h
	llvm/LLVMNode.cpp
	llvm/LLVMDependenceGraph.h
	llvm/LLVMDependenceGraph.cpp
	llvm/LLVMDGVerifier.h
	llvm/LLVMDGVerifier.cpp
	llvm/PostDominators.cpp
	llvm/AnalysisGeneric.h
	llvm/AnalysisGeneric.cpp
	llvm/PointsTo.h
	llvm/PointsTo.cpp
	llvm/DefUse.h
	llvm/DefUse.cpp
	llvm/DefMap.h
	llvm/DefMap.cpp
	llvm/ReachingDefs.h
	llvm/ReachingDefs.cpp
	llvm/Slicer.h
	llvm/PSS.h
	llvm/PSS.cpp
	llvm/LLVMPointsToAnalysis.h
	llvm/LLVMPointsToAnalysis.cpp
	llvm/LLVMReachingDefinitions.h
	llvm/LLVMReachingDefinitions.cpp
)

target_link_libraries(LLVMdg PSS RD ${llvm_libs})
install(TARGETS LLVMdg PSS RD
	LIBRARY DESTINATION lib)

if (ENABLE_DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g -Wall -Wextra -O0")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O2")
endif ()

endif(LLVM_DG)
