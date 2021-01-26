# use assertions in tests
string(REGEX REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

include_directories(${CMAKE_SOURCE_DIR}/include)

# add check target
enable_testing()

include(ProcessorCount)
ProcessorCount(N)
if(N EQUAL 0)
    set(N 1)
endif()

set(CTEST_OPTS -j${N} --output-on-failure --progress ${CTEST_OPTS})
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} ${CTEST_OPTS}
                        USES_TERMINAL)

# --------------------------------------------------
# precompile Catch2 main
# --------------------------------------------------
add_library(catch-main OBJECT catch-main.cpp)

macro(add_catch_test TEST_FILE)
    if (NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${TEST_FILE}")
        message(FATAL_ERROR "Test '${TEST_FILE}' does not exist!")
    endif()
    get_filename_component(TEST ${TEST_FILE} NAME_WE)

    add_executable(${TEST} ${TEST_FILE} $<TARGET_OBJECTS:catch-main>)
    add_test(${TEST} ${TEST})
    add_dependencies(check ${TEST})
endmacro()

# --------------------------------------------------
# points-to-test
# --------------------------------------------------
add_catch_test(points-to-test.cpp)
target_link_libraries(points-to-test PRIVATE dgpta)

# --------------------------------------------------
# readwritegraph-test
# --------------------------------------------------
add_catch_test(readwritegraph-test.cpp)
target_link_libraries(readwritegraph-test PRIVATE dgdda)

# --------------------------------------------------
# adt-test
# --------------------------------------------------
add_catch_test(adt-test.cpp)
target_link_libraries(adt-test PRIVATE dganalysis)

# --------------------------------------------------
# bitvector-test
# --------------------------------------------------
add_catch_test(bitvector-test.cpp)

# --------------------------------------------------
# numbers-set-test
# --------------------------------------------------
add_catch_test(numbers-set-test.cpp)

# --------------------------------------------------
# points-to-set-test
# --------------------------------------------------
add_catch_test(points-to-set-test.cpp)
target_link_libraries(points-to-set-test PRIVATE dganalysis dgpta)

# --------------------------------------------------
# disjunctive-intervals-map-test
# --------------------------------------------------
add_catch_test(disjunctive-intervals-map-test.cpp)
target_link_libraries(disjunctive-intervals-map-test PRIVATE dganalysis)

# --------------------------------------------------
# nodes-walk-test
# --------------------------------------------------
add_catch_test(nodes-walk-test.cpp)

# --------------------------------------------------
# fuzzing tests
# --------------------------------------------------
if(TESTING_ENABLE_FUZZING)
    add_subdirectory(fuzzing)
endif()

# --------------------------------------------------
# ThreadRegions test
# --------------------------------------------------

add_custom_command(OUTPUT simple.ll pthread_exit.ll
                   COMMAND clang -S -emit-llvm ${CMAKE_CURRENT_LIST_DIR}/thread-regions-test-files/simple.c
                   COMMAND clang -S -emit-llvm ${CMAKE_CURRENT_LIST_DIR}/thread-regions-test-files/pthread_exit.c
                   DEPENDS ${CMAKE_CURRENT_LIST_DIR}/thread-regions-test-files/simple.c
                           ${CMAKE_CURRENT_LIST_DIR}/thread-regions-test-files/pthread_exit.c)

add_custom_target(thread-regions-test-file DEPENDS simple.ll)

add_catch_test(thread-regions-test.cpp)
add_dependencies(thread-regions-test thread-regions-test-file)

target_compile_definitions(thread-regions-test
    PRIVATE
        SIMPLE_FILE="${CMAKE_CURRENT_BINARY_DIR}/simple.ll"
        PTHREAD_EXIT_FILE="${CMAKE_CURRENT_BINARY_DIR}/pthread_exit.ll")

target_link_libraries(thread-regions-test PRIVATE dgllvmthreadregions
                                          PRIVATE ${llvm_core}
                                          PRIVATE ${llvm_irreader}
                                          PRIVATE ${llvm_support})

# --------------------------------------------------
# llvm-dg-test
# --------------------------------------------------
add_catch_test(llvm-dg-test.cpp)
target_link_libraries(llvm-dg-test PRIVATE dgllvmdg
                                   PRIVATE ${llvm_core}
                                   PRIVATE ${llvm_irreader}
                                   PRIVATE ${llvm_analysis})

# --------------------------------------------------
# slicing tests
# --------------------------------------------------
add_subdirectory(slicing)
add_dependencies(check llvm-slicer)

# --------------------------------------------------
# benchmarking
# --------------------------------------------------
add_executable(ptset-benchmark ptset-benchmark.cpp)
target_link_libraries(ptset-benchmark PRIVATE dganalysis dgpta)
