if (LLVM_DG)
	set(LLVM_LINK_COMPONENTS core engine asmparser bitreader)
endif (LLVM_DG)

include_directories('../src')

add_executable(dg-test dg-test.cpp)
target_link_libraries(dg-test dg)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if (DEBUG)
	list(APPEND CMAKE_CXX_FLAGS "-g")
endif (DEBUG)
