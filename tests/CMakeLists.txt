if (ENABLE_DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g -O0")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O2")
endif ()

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
# llvm-dg-test
# --------------------------------------------------
if (LLVM_DG)
	set(LLVM_LINK_COMPONENTS core engine asmparser bitreader)
	add_executable(llvm-dg-test llvm-dg-test.cpp)
	target_link_libraries(llvm-dg-test llvmdg)

	add_test(llvm-dg-test llvm-dg-test)
	add_dependencies(check llvm-dg-test)

	add_test(slicing-test1 slicing-test1.sh)
	add_test(slicing-test2 slicing-test2.sh)
	add_test(slicing-test3 slicing-test3.sh)
	add_test(slicing-interprocedural1 slicing-interprocedural1.sh)
	add_test(slicing-interprocedural2 slicing-interprocedural2.sh)
	add_test(slicing-interprocedural3 slicing-interprocedural3.sh)
	add_test(slicing-funcptr1 slicing-funcptr1.sh)
	add_test(slicing-funcptr2 slicing-funcptr2.sh)
	add_test(slicing-funcptr3 slicing-funcptr3.sh)
	add_test(slicing-funcptr4 slicing-funcptr4.sh)
	add_test(slicing-funcptr5 slicing-funcptr5.sh)
	add_test(slicing-funcptr6 slicing-funcptr6.sh)
	add_test(slicing-unknownptr1 slicing-unknownptr1.sh)
	add_test(slicing-unknownptr2 slicing-unknownptr2.sh)
	add_test(slicing-unknownptr3 slicing-unknownptr3.sh)
	add_test(slicing-phi1 slicing-phi1.sh)
	add_test(slicing-phi2 slicing-phi2.sh)
	add_test(slicing-global1 slicing-global1.sh)
	add_test(slicing-global2 slicing-global2.sh)
	add_test(slicing-ptrtoint1 slicing-ptrtoint1.sh)

endif (LLVM_DG)
