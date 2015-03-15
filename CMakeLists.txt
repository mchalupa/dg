cmake_minimum_required(VERSION 2.8)

# define DEBUG by default for now
OPTION(ENABLE_DEBUG "Enable debugging" ON)
OPTION(LLVM_DG "Support for LLVM Dependency graph" ON)
OPTION(ENABLE_CFG "Add CFG edges to the graph" ON)

if (ENABLE_CFG)
	add_definitions(-DENABLE_CFG)
endif()

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

	add_definitions(-DHAVE_LLVM)
endif(LLVM_DG)

if (ENABLE_DEBUG)
	add_definitions(-DDEBUG_ENABLED)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g -O0")
else ()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O2")
endif ()

add_subdirectory(src)
add_subdirectory(tests)

enable_testing()
