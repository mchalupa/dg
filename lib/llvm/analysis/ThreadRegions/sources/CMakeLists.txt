include(${CMAKE_CURRENT_LIST_DIR}/Nodes/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/Graphs/CMakeLists.txt)

target_sources(ThreadRegions 
		PRIVATE
			${CMAKE_CURRENT_LIST_DIR}/ThreadRegion.cpp
                        ${CMAKE_CURRENT_LIST_DIR}/MayHappenInParallel.cpp)
