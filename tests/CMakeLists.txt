# use assertions in tests
string(REGEX REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

include_directories(${CMAKE_SOURCE_DIR}/include)

# add check target
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND})

# --------------------------------------------------
# dg-test
# --------------------------------------------------
add_executable(dg-test dg-test.cpp)
add_test(dg-test dg-test)
add_dependencies(check dg-test)

# --------------------------------------------------
# data-flow-test
# --------------------------------------------------
add_executable(data-flow-test data-flow-test.cpp)
add_test(data-flow-test data-flow-test)
add_dependencies(check data-flow-test)

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
# fuzzing regressions
# --------------------------------------------------
add_executable(numbers-set1-regressions
	       numbers-set1-regressions.cpp fuzzing/numbers-set1.cpp)
add_dependencies(check numbers-set1-regressions)
add_test(numbers-set1-regressions numbers-set1-regressions)

add_executable(disjunctive-interval-map-regressions
	       disjunctive-interval-map-regressions.cpp fuzzing/disjunctive-map1.cpp)
target_compile_definitions(disjunctive-interval-map-regressions PRIVATE "-DDUMP")
add_dependencies(check disjunctive-interval-map-regressions)
add_test(disjunctive-interval-map-regressions disjunctive-interval-map-regressions)

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
if (LLVM_DG)
	set(LLVM_LINK_COMPONENTS core engine asmparser bitreader)
	add_executable(llvm-dg-test llvm-dg-test.cpp)
	target_link_libraries(llvm-dg-test
				PRIVATE dgllvmdg
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis})

	add_test(llvm-dg-test llvm-dg-test)
	add_dependencies(check llvm-dg-test)

	add_test(slicing-test1 run-slicing-test.sh slicing-test1.sh)
	add_test(slicing-test2 run-slicing-test.sh slicing-test2.sh)
	add_test(slicing-test3 run-slicing-test.sh slicing-test3.sh)
	add_test(slicing-test4 run-slicing-test.sh slicing-test4.sh)
	add_test(slicing-test5 run-slicing-test.sh slicing-test5.sh)
	add_test(slicing-test6 run-slicing-test.sh slicing-test6.sh)
	add_test(slicing-test7 run-slicing-test.sh slicing-test7.sh)
	add_test(slicing-test8 run-slicing-test.sh slicing-test8.sh)
	add_test(slicing-test22 run-slicing-test.sh slicing-test22.sh)
	add_test(slicing-test222 run-slicing-test.sh slicing-test222.sh)
	add_test(slicing-recursive1 run-slicing-test.sh slicing-recursive1.sh)
	add_test(slicing-recursive2 run-slicing-test.sh slicing-recursive2.sh)
	add_test(slicing-recursive3 run-slicing-test.sh slicing-recursive3.sh)
	add_test(slicing-recursive4 run-slicing-test.sh slicing-recursive4.sh)
	add_test(slicing-recursive5 run-slicing-test.sh slicing-recursive5.sh)
	add_test(slicing-interprocedural1 run-slicing-test.sh slicing-interprocedural1.sh)
	add_test(slicing-interprocedural2 run-slicing-test.sh slicing-interprocedural2.sh)
	add_test(slicing-interprocedural3 run-slicing-test.sh slicing-interprocedural3.sh)
	add_test(slicing-interprocedural4 run-slicing-test.sh slicing-interprocedural4.sh)
	add_test(slicing-interprocedural5 run-slicing-test.sh slicing-interprocedural5.sh)
	add_test(slicing-interprocedural6 run-slicing-test.sh slicing-interprocedural6.sh)
	add_test(slicing-interprocedural7 run-slicing-test.sh slicing-interprocedural7.sh)
	add_test(slicing-interprocedural8 run-slicing-test.sh slicing-interprocedural8.sh)
	add_test(slicing-interprocedural9 run-slicing-test.sh slicing-interprocedural9.sh)
	add_test(slicing-funcptr1 run-slicing-test.sh slicing-funcptr1.sh)
	add_test(slicing-funcptr2 run-slicing-test.sh slicing-funcptr2.sh)
	add_test(slicing-funcptr3 run-slicing-test.sh slicing-funcptr3.sh)
	add_test(slicing-funcptr4 run-slicing-test.sh slicing-funcptr4.sh)
	add_test(slicing-funcptr5 run-slicing-test.sh slicing-funcptr5.sh)
	add_test(slicing-funcptr6 run-slicing-test.sh slicing-funcptr6.sh)
	add_test(slicing-funcptr7 run-slicing-test.sh slicing-funcptr7.sh)
	add_test(slicing-funcptr8 run-slicing-test.sh slicing-funcptr8.sh)
	add_test(slicing-funcptr9 run-slicing-test.sh slicing-funcptr9.sh)
	add_test(slicing-funcptr10 run-slicing-test.sh slicing-funcptr10.sh)
	add_test(slicing-funcptr11 run-slicing-test.sh slicing-funcptr11.sh)
	add_test(slicing-funcptr12 run-slicing-test.sh slicing-funcptr12.sh)
	add_test(slicing-funcptr13 run-slicing-test.sh slicing-funcptr13.sh)
	add_test(slicing-funcptr14 run-slicing-test.sh slicing-funcptr14.sh)
	add_test(slicing-funcptr15 run-slicing-test.sh slicing-funcptr15.sh)
	add_test(slicing-funcptr16 run-slicing-test.sh slicing-funcptr16.sh)
	add_test(slicing-funcptr-regression1 run-slicing-test.sh slicing-funcptr-regression1.sh)
	add_test(slicing-funcarray1 run-slicing-test.sh slicing-funcarray1.sh)
	add_test(slicing-funcarray2 run-slicing-test.sh slicing-funcarray2.sh)
	add_test(slicing-funcarray3 run-slicing-test.sh slicing-funcarray3.sh)
	add_test(slicing-unknownptr1 run-slicing-test.sh slicing-unknownptr1.sh)
	add_test(slicing-unknownptr2 run-slicing-test.sh slicing-unknownptr2.sh)
	add_test(slicing-unknownptr3 run-slicing-test.sh slicing-unknownptr3.sh)
	add_test(slicing-unknownptr4 run-slicing-test.sh slicing-unknownptr4.sh)
	add_test(slicing-unknownptr5 run-slicing-test.sh slicing-unknownptr5.sh)
	add_test(slicing-unknownptr6 run-slicing-test.sh slicing-unknownptr6.sh)
	add_test(slicing-unknownptr7 run-slicing-test.sh slicing-unknownptr7.sh)
	add_test(slicing-unknownptr8 run-slicing-test.sh slicing-unknownptr8.sh)
	add_test(slicing-pointers1 run-slicing-test.sh slicing-pointers1.sh)
	add_test(slicing-pointers2 run-slicing-test.sh slicing-pointers2.sh)
	add_test(slicing-pointers3 run-slicing-test.sh slicing-pointers3.sh)
	add_test(slicing-pointers4 run-slicing-test.sh slicing-pointers4.sh)
	add_test(slicing-pointers5 run-slicing-test.sh slicing-pointers5.sh)
	add_test(slicing-pointers6 run-slicing-test.sh slicing-pointers6.sh)
	add_test(slicing-pointers7 run-slicing-test.sh slicing-pointers7.sh)
	add_test(slicing-ptrarray1 run-slicing-test.sh slicing-ptrarray1.sh)
	add_test(slicing-ptrarray2 run-slicing-test.sh slicing-ptrarray2.sh)
	add_test(slicing-phi1 run-slicing-test.sh slicing-phi1.sh)
	add_test(slicing-phi2 run-slicing-test.sh slicing-phi2.sh)
	add_test(slicing-phi3 run-slicing-test.sh slicing-phi3.sh)
	add_test(slicing-phi4 run-slicing-test.sh slicing-phi4.sh)
	add_test(slicing-global1 run-slicing-test.sh slicing-global1.sh)
	add_test(slicing-global2 run-slicing-test.sh slicing-global2.sh)
	add_test(slicing-global3 run-slicing-test.sh slicing-global3.sh)
	add_test(slicing-global4 run-slicing-test.sh slicing-global4.sh)
	add_test(slicing-global5 run-slicing-test.sh slicing-global5.sh)
	add_test(slicing-global6 run-slicing-test.sh slicing-global6.sh)
	add_test(slicing-global7 run-slicing-test.sh slicing-global7.sh)
	add_test(slicing-global8 run-slicing-test.sh slicing-global8.sh)
	add_test(slicing-global9 run-slicing-test.sh slicing-global9.sh)
	add_test(slicing-global10 run-slicing-test.sh slicing-global10.sh)
	add_test(slicing-ptrtoint1 run-slicing-test.sh slicing-ptrtoint1.sh)
	add_test(slicing-ptrtoint2 run-slicing-test.sh slicing-ptrtoint2.sh)
	add_test(slicing-ptrtoint3 run-slicing-test.sh slicing-ptrtoint3.sh)
	add_test(slicing-ptrtoint4 run-slicing-test.sh slicing-ptrtoint4.sh)
	add_test(slicing-ptrtoint5 run-slicing-test.sh slicing-ptrtoint5.sh)
	add_test(slicing-ptrtoint6 run-slicing-test.sh slicing-ptrtoint6.sh)
	add_test(slicing-ptrtoint7 run-slicing-test.sh slicing-ptrtoint7.sh)
	add_test(slicing-llvmmemcpy run-slicing-test.sh slicing-llvmmemcpy.sh)
	add_test(slicing-llvmmemcpy2 run-slicing-test.sh slicing-llvmmemcpy2.sh)
	add_test(slicing-memset1 run-slicing-test.sh slicing-memset1.sh)
	add_test(slicing-memcpy1 run-slicing-test.sh slicing-memcpy1.sh)
	add_test(slicing-memcpy2 run-slicing-test.sh slicing-memcpy2.sh)
	add_test(slicing-memcpy3 run-slicing-test.sh slicing-memcpy3.sh)
	add_test(slicing-memcpy4 run-slicing-test.sh slicing-memcpy4.sh)
	add_test(slicing-memcpy5 run-slicing-test.sh slicing-memcpy5.sh)
	add_test(slicing-memcpy6 run-slicing-test.sh slicing-memcpy6.sh)
	add_test(slicing-bitcast1 run-slicing-test.sh slicing-bitcast1.sh)
	add_test(slicing-bitcast2 run-slicing-test.sh slicing-bitcast2.sh)
	add_test(slicing-bitcast3 run-slicing-test.sh slicing-bitcast3.sh)
	add_test(slicing-bitcast4 run-slicing-test.sh slicing-bitcast4.sh)
	add_test(slicing-bitcast5 run-slicing-test.sh slicing-bitcast5.sh)
	add_test(slicing-loop1 run-slicing-test.sh slicing-loop1.sh)
	add_test(slicing-loop2 run-slicing-test.sh slicing-loop2.sh)
	add_test(slicing-loop3 run-slicing-test.sh slicing-loop3.sh)
	add_test(slicing-loop4 run-slicing-test.sh slicing-loop4.sh)
	add_test(slicing-loop5 run-slicing-test.sh slicing-loop5.sh)
	add_test(slicing-list1 run-slicing-test.sh slicing-list1.sh)
	add_test(slicing-list2 run-slicing-test.sh slicing-list2.sh)
	add_test(slicing-list3 run-slicing-test.sh slicing-list3.sh)
	add_test(slicing-list4 run-slicing-test.sh slicing-list4.sh)
	add_test(slicing-list5 run-slicing-test.sh slicing-list5.sh)
	add_test(slicing-list6 run-slicing-test.sh slicing-list6.sh)
	add_test(slicing-list7 run-slicing-test.sh slicing-list7.sh)
	add_test(slicing-list8 run-slicing-test.sh slicing-list8.sh)
	add_test(slicing-list9 run-slicing-test.sh slicing-list9.sh)
	add_test(slicing-dynalloc1 run-slicing-test.sh slicing-dynalloc1.sh)
	add_test(slicing-dynalloc2 run-slicing-test.sh slicing-dynalloc2.sh)
	add_test(slicing-dynalloc3 run-slicing-test.sh slicing-dynalloc3.sh)
	add_test(slicing-dynalloc4 run-slicing-test.sh slicing-dynalloc4.sh)
	add_test(slicing-dynalloc5 run-slicing-test.sh slicing-dynalloc5.sh)
	add_test(slicing-dynalloc6 run-slicing-test.sh slicing-dynalloc6.sh)
	add_test(slicing-dynalloc7 run-slicing-test.sh slicing-dynalloc7.sh)
	add_test(slicing-realloc1 run-slicing-test.sh slicing-realloc1.sh)
	add_test(slicing-realloc2 run-slicing-test.sh slicing-realloc2.sh)
	add_test(slicing-switch1 run-slicing-test.sh slicing-switch1.sh)
	add_test(slicing-switch2 run-slicing-test.sh slicing-switch2.sh)
	add_test(slicing-vararg1 run-slicing-test.sh slicing-vararg1.sh)
	add_test(slicing-vararg2 run-slicing-test.sh slicing-vararg2.sh)
	add_test(slicing-vararg3 run-slicing-test.sh slicing-vararg3.sh)
	add_test(slicing-vararg4 run-slicing-test.sh slicing-vararg4.sh)
	add_test(slicing-negoffset1 run-slicing-test.sh slicing-negoffset1.sh)
	add_test(slicing-negoffset2 run-slicing-test.sh slicing-negoffset2.sh)
	add_test(slicing-negoffset3 run-slicing-test.sh slicing-negoffset3.sh)
	add_test(slicing-sum1 run-slicing-test.sh slicing-sum1.sh)
	add_test(slicing-sum2 run-slicing-test.sh slicing-sum2.sh)
	add_test(slicing-sum3 run-slicing-test.sh slicing-sum3.sh)
	add_test(slicing-threads1 run-slicing-threaded-test.sh slicing-threads1.sh)
	add_test(control-regression1 run-slicing-test.sh slicing-control-regression1.sh)
	add_test(pta_fs_regression1 run-slicing-test.sh slicing-pta_fs_regression1.sh)
	add_test(pta_regression2 run-slicing-test.sh slicing-pta_regression2.sh)
	add_test(alias_of_return run-slicing-test.sh slicing-alias_of_return.sh)
	add_test(regression1 run-slicing-test.sh slicing-regression1.sh)
	add_test(fptoui run-slicing-test.sh slicing-fptoui1.sh)
	add_test(malloc-redef run-slicing-test.sh slicing-malloc-redef.sh)
	add_test(globalptr1 run-slicing-test.sh slicing-globalptr1.sh)
	add_test(globalptr2 run-slicing-test.sh slicing-globalptr2.sh)
	add_test(globalptr3 run-slicing-test.sh slicing-globalptr3.sh)
	add_test(globalptr4 run-slicing-test.sh slicing-globalptr4.sh)
	add_test(pta-inv-infinite-loop run-slicing-test.sh pta-inv-infinite-loop.sh)

endif (LLVM_DG)

# --------------------------------------------------
# benchmarking
# --------------------------------------------------
add_executable(rdmap-benchmark rdmap-benchmark.cpp)
target_link_libraries(rdmap-benchmark dgdda)

add_executable(ptset-benchmark ptset-benchmark.cpp)
target_link_libraries(ptset-benchmark PRIVATE dganalysis)

