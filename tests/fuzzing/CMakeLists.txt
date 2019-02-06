# use assertions in tests
string(REGEX REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize=address,undefined,fuzzer")

include_directories(${CMAKE_SOURCE_DIR}/include)

# --------------------------------------------------
# fuzzing tests
# --------------------------------------------------
add_executable(numbers-set1 numbers-set1.cpp)
add_dependencies(check numbers-set1)
add_test(numbers-set1-fuzzing numbers-set1 -runs=100000)

