# use assertions in tests
string(REGEX REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

include_directories(${CMAKE_SOURCE_DIR}/include)

# add check target
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND})

# --------------------------------------------------
# points-to-test
# --------------------------------------------------
add_executable(points-to-test points-to-test.cpp)
add_test(points-to-test points-to-test)
add_dependencies(check points-to-test)
target_link_libraries(points-to-test PRIVATE dgpta)

# --------------------------------------------------
# rdmap-test
# DISABLED for now: it contains just a one that case
# that does nothing and even though it behaved weirdly
# on some machines for no reason.
# --------------------------------------------------
# add_executable(rdmap-test rdmap-test.cpp)
# target_link_libraries(rdmap-test PRIVATE dganalysis)
# add_test(rdmap-test rdmap-test)
# add_dependencies(check rdmap-test)
# target_link_libraries(rdmap-test PRIVATE dgdda)
# target_include_directories(rdmap-test PRIVATE ${CMAKE_SOURCE_DIR}/lib)

# --------------------------------------------------
# reaching-definitions-test
# --------------------------------------------------
add_executable(reaching-definitions-test reaching-definitions-test.cpp)
add_test(reaching-definitions-test reaching-definitions-test)
add_dependencies(check reaching-definitions-test)
target_link_libraries(reaching-definitions-test PRIVATE dgdda)

# --------------------------------------------------
# adt-test
# --------------------------------------------------
add_executable(adt-test adt-test.cpp)
target_link_libraries(adt-test PRIVATE dganalysis)
add_test(adt-test adt-test)
add_dependencies(check adt-test)

# --------------------------------------------------
# bitvector-test
# --------------------------------------------------
add_executable(bitvector-test bitvector-test.cpp)
add_test(bitvector-test bitvector-test)
add_dependencies(check bitvector-test)

# --------------------------------------------------
# numbers-set-test
# --------------------------------------------------
add_executable(numbers-set-test numbers-set-test.cpp)
add_test(numbers-set-test numbers-set-test)
add_dependencies(check numbers-set-test)

# --------------------------------------------------
# points-to-set-test
# --------------------------------------------------
add_executable(points-to-set-test points-to-set-test.cpp)
target_link_libraries(points-to-set-test PRIVATE dganalysis dgpta)
add_test(points-to-set-test points-to-set-test)
add_dependencies(check points-to-set-test)

# --------------------------------------------------
# disjunctive-intervals-map-test
# --------------------------------------------------
add_executable(disjunctive-intervals-map-test disjunctive-intervals-map-test.cpp)
target_link_libraries(disjunctive-intervals-map-test PRIVATE dganalysis)
add_test(disjunctive-intervals-map-test disjunctive-intervals-map-test)
add_dependencies(check disjunctive-intervals-map-test)
target_include_directories(disjunctive-intervals-map-test PRIVATE ${CMAKE_SOURCE_DIR}/lib)

# --------------------------------------------------
# nodes-walk-test
# --------------------------------------------------
add_executable(nodes-walk-test nodes-walk-test.cpp)
add_test(nodes-walk-test nodes-walk-test)
add_dependencies(check nodes-walk-test)

# --------------------------------------------------
# fuzzing tests
# --------------------------------------------------
if (TESTING_ENABLE_FUZZING)
	add_subdirectory(fuzzing)
endif(TESTING_ENABLE_FUZZING)

# --------------------------------------------------
# ThreadRegions test
# --------------------------------------------------

add_custom_command(OUTPUT simple.ll pthread_exit.ll
                   COMMAND clang -S -emit-llvm ${CMAKE_CURRENT_LIST_DIR}/thread-regions-test-files/simple.c
                   COMMAND clang -S -emit-llvm ${CMAKE_CURRENT_LIST_DIR}/thread-regions-test-files/pthread_exit.c
                   DEPENDS ${CMAKE_CURRENT_LIST_DIR}/thread-regions-test-files/simple.c ${CMAKE_CURRENT_LIST_DIR}/thread-regions-test-files/pthread_exit.c )

add_custom_target(thread-regions-test-file DEPENDS simple.ll)

add_executable(thread-regions-test ${CMAKE_CURRENT_LIST_DIR}/catch-main.cpp
                                  ${CMAKE_CURRENT_LIST_DIR}/thread-regions-test.cpp)
add_dependencies(thread-regions-test thread-regions-test-file)

target_compile_definitions(thread-regions-test
    PRIVATE
        SIMPLE_FILE="${CMAKE_CURRENT_BINARY_DIR}/simple.ll"
        PTHREAD_EXIT_FILE="${CMAKE_CURRENT_BINARY_DIR}/pthread_exit.ll")

target_link_libraries(thread-regions-test PRIVATE dgllvmthreadregions
                                          PRIVATE ${llvm_core}
                                          PRIVATE ${llvm_irreader}
                                          PRIVATE ${llvm_support})
add_test(thread-regions-test thread-regions-test)
add_dependencies(check thread-regions-test)

# --------------------------------------------------
# llvm-dg-test
# --------------------------------------------------
add_executable(llvm-dg-test llvm-dg-test.cpp)
target_link_libraries(llvm-dg-test
			PRIVATE dgllvmdg
			PRIVATE ${llvm_irreader}
			PRIVATE ${llvm_analysis})

add_test(llvm-dg-test llvm-dg-test)
add_dependencies(check llvm-dg-test)

# --------------------------------------------------
# slicing tests
# --------------------------------------------------
add_subdirectory(slicing)

# --------------------------------------------------
# benchmarking
# --------------------------------------------------
add_executable(rdmap-benchmark rdmap-benchmark.cpp)
target_link_libraries(rdmap-benchmark dgdda)

add_executable(ptset-benchmark ptset-benchmark.cpp)
target_link_libraries(ptset-benchmark PRIVATE dganalysis)

