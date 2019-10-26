# use assertions in tests
string(REGEX REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize=address,undefined,fuzzer")

include_directories(${CMAKE_SOURCE_DIR}/include)

# --------------------------------------------------
# fuzzing tests
# --------------------------------------------------
add_executable(numbers-set1 numbers-set1.cpp)
add_dependencies(check numbers-set1)
add_test(numbers-set1-fuzzing numbers-set1 regressions/numbers-set1 -runs=100000)

add_executable(bitvector1 bitvector1.cpp)
add_dependencies(check bitvector1)
add_test(bitvector1-fuzzing bitvector1 -runs=100000)

add_executable(disjunctive-map1 disjunctive-map1.cpp)
add_dependencies(check disjunctive-map1)
add_test(disjunctive-map1-fuzzing disjunctive-map1 regressions/disjunctive-map1 -runs=20000)

