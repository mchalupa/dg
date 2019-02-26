target_include_directories(ControlDependence
    PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_sources(ControlDependence
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/Block.cpp
            ${CMAKE_CURRENT_LIST_DIR}/Function.cpp
            ${CMAKE_CURRENT_LIST_DIR}/GraphBuilder.cpp)
