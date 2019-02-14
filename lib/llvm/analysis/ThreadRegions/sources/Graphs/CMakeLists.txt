target_sources(ThreadRegions
        PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/BlockGraph.cpp
            ${CMAKE_CURRENT_LIST_DIR}/FunctionGraph.cpp
            ${CMAKE_CURRENT_LIST_DIR}/GraphBuilder.cpp
            ${CMAKE_CURRENT_LIST_DIR}/ThreadRegionsBuilder.cpp
            ${CMAKE_CURRENT_LIST_DIR}/CriticalSectionsBuilder.cpp
            ${CMAKE_CURRENT_LIST_DIR}/ControlFlowGraph.cpp)
