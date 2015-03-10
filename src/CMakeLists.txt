add_library(dg SHARED
	DependenceGraph.h
	DGUtil.cpp
	DGUtil.h
	LLVMDependenceGraph.h
)

if (ENABLE_DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g -O0")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O2")
endif ()
