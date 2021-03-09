# --------------------------------------------------
# use sanitizers in fuzzing tests
# --------------------------------------------------
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined,fuzzer")

# --------------------------------------------------
# fuzzing tests
# --------------------------------------------------
add_executable(numbers-set1 numbers-set1.cpp)
add_dependencies(check numbers-set1)
add_test(numbers-set1-fuzzing numbers-set1
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/numbers-set1 -runs=100000)

add_executable(bitvector1 bitvector1.cpp)
add_dependencies(check bitvector1)
add_test(bitvector1-fuzzing bitvector1 -runs=100000)

add_executable(disjunctive-map1 disjunctive-map1.cpp)
add_dependencies(check disjunctive-map1)
add_test(disjunctive-map1-fuzzing disjunctive-map1
    ${CMAKE_CURRENT_SOURCE_DIR}/regressions/disjunctive-map1 -runs=20000)
