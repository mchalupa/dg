if (LLVM_DG)
	set(LLVM_LINK_COMPONENTS core engine asmparser bitreader)
endif (LLVM_DG)

add_executable(dg-dump dg-dump.cpp)
target_link_libraries(dg-dump libdg)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if (DEBUG)
	list(APPEND CMAKE_CXX_FLAGS "-g")
endif (DEBUG)
