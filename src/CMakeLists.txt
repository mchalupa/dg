add_library(libdg SHARED
	DependenceGraph.cpp
	DependenceGraph.h
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if (DEBUG)
	list(APPEND CMAKE_CXX_FLAGS "-g")
endif (DEBUG)
