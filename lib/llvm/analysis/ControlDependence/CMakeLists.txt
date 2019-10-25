target_include_directories(dgllvmcda
    PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_sources(dgllvmcda
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/Block.cpp
            ${CMAKE_CURRENT_LIST_DIR}/Function.cpp
            ${CMAKE_CURRENT_LIST_DIR}/GraphBuilder.cpp
            ${CMAKE_CURRENT_LIST_DIR}/NonTerminationSensitiveControlDependencyAnalysis.cpp)
