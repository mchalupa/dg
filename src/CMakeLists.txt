if (LLVM_DG)
  set(LLVM_LINK_COMPONENTS core engine asmparser bitreader support)

add_library(llvmdg SHARED
	Utils.h
	BBlock.h
	Node.h
	DependenceGraph.h
	EdgesContainer.h
	llvm/DependenceGraph.h
	llvm/DependenceGraph.cpp
)

target_link_libraries(llvmdg ${llvm_libs})

if (ENABLE_DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g -O0")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O2")
endif ()

endif(LLVM_DG)
