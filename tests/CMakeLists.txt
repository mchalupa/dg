# use debug info in tests
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
# find compatible clang, lli, llvm-link and opt
# --------------------------------------------------

# clang binary uses only major version as suffix since 7.0
if(${LLVM_PACKAGE_VERSION} VERSION_LESS "7.0")
    set(CLANG_BINARY clang-${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR})
else()
    set(CLANG_BINARY clang-${LLVM_VERSION_MAJOR})
endif()

# REQUIRED available with CMake 3.18+
find_program(LLVMCC ${CLANG_BINARY} HINTS ${LLVM_TOOLS_BINARY_DIR}
                                    # do not use ccache
                                    NO_SYSTEM_ENVIRONMENT_PATH)
if(NOT LLVMCC)
    message(FATAL_ERROR "Bitcode compiler: version compatible with \
                         LLVM ${LLVM_PACKAGE_VERSION} not found")
endif()
message(STATUS "Bitcode compiler: ${LLVMCC}")

foreach(TOOL LLVM-LINK LLI OPT)
    string(TOLOWER ${TOOL} TOOL_LOWER)

    # REQUIRED available only with CMake 3.18+
    find_program(${TOOL} ${TOOL_LOWER} PATHS ${LLVM_TOOLS_BINARY_DIR}
                                       NO_DEFAULT_PATH)
    if(NOT ${TOOL})
        message(FATAL_ERROR "${TOOL_LOWER}: version compatible with \
                             LLVM ${LLVM_PACKAGE_VERSION} not found")
    endif()
    message(STATUS "${TOOL_LOWER}: ${${TOOL}}")
endforeach()

# FIXME:
# This is a rather ugly hack to always use the correct path in test-runner.py.
# Using configure_file to generate the correct script would probably be much
# nicer.

# all tools have the same dirname
set(LLVM_TOOLS_DIR "${LLVM_TOOLS_BINARY_DIR}" CACHE PATH
                   "contains lli ${LLVM_PACKAGE_VERSION}, etc." FORCE)

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
if(ENABLE_FUZZING)
    add_subdirectory(fuzzing)
endif()

# --------------------------------------------------
# ThreadRegions test
# --------------------------------------------------

add_custom_command(OUTPUT simple.ll pthread_exit.ll
                   COMMAND ${LLVMCC} -S -emit-llvm ${CMAKE_CURRENT_LIST_DIR}/thread-regions-test-files/simple.c
                   COMMAND ${LLVMCC} -S -emit-llvm ${CMAKE_CURRENT_LIST_DIR}/thread-regions-test-files/pthread_exit.c
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
