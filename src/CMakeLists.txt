if (LLVM_DG)
  set(LLVM_LINK_COMPONENTS core engine asmparser bitreader support)
endif()

add_library(dg SHARED
	DependenceGraph.h
	DGUtil.cpp
	DGUtil.h
	LLVMDependenceGraph.h
	LLVMDependenceGraph.cpp
)

target_link_libraries(dg ${llvm_libs})

if (ENABLE_DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g -O0")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O2")
endif ()
