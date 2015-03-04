cmake_minimum_required(VERSION 2.8)

# define DEBUG by default for now
add_definitions(-DDEBUG)

if (LLVM_DG)
	find_package(LLVM REQUIRED CONFIG)
	include(AddLLVM)

	message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
	message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

	add_definitions(${LLVM_DEFINITIONS})
	include_directories(${LLVM_INCLUDE_DIRS})
	link_directories(${LLVM_LIBRARY_DIRS})

	if (LLVM_SRC_PATH)
	  include_directories(${LLVM_SRC_PATH}/include)
	endif(LLVM_SRC_PATH)

	if (LLVM_BUILD_PATH)
	  link_directories(${LLVM_BUILD_PATH}/lib)
	endif(LLVM_BUILD_PATH)

	llvm_replace_compiler_option(CMAKE_CXX_FLAGS "-frtti" "-fno-rtti")
endif(LLVM_DG)

if (CMAKE_CXX_FLAGS)
	list(APPEND CMAKE_CXX_FLAGS "-std=c++11")
else(CMAKE_CXX_FLAGS)
	set(CMAKE_CXX_FLAGS "-std=c++11")
endif (CMAKE_CXX_FLAGS)

if (DEBUG)
	list(APPEND CMAKE_CXX_FLAGS "-g")
endif (DEBUG)

add_subdirectory(src)
add_subdirectory(tests)

enable_testing()
