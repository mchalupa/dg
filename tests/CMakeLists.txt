include_directories('../src')

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

endif (LLVM_DG)
