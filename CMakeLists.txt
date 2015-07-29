cmake_minimum_required(VERSION 2.8)
project(dg)

include(CTest)

include_directories( ${CMAKE_SOURCE_DIR}/src )

# define DEBUG by default for now
OPTION(ENABLE_DEBUG "Enable debugging" ON)
OPTION(LLVM_DG "Support for LLVM Dependency graph" ON)
OPTION(ENABLE_CFG "Add support for CFG edges to the graph" ON)
OPTION(ENABLE_POSTDOM "Add support for post-dominator edges to the graph" ON)

if (LLVM_DG)
	# for llvm dg we need cfg and postdom edges
	if (NOT ENABLE_CFG OR NOT ENABLE_POSTDOM)
		message(STATUS "Enabling CFG and post-dom edges due to llvm dg")
	endif()

	set(ENABLE_CFG ON)
	set(ENABLE_POSTDOM ON)

	find_package(LLVM REQUIRED CONFIG)

	message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
	message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

	if (LLVM_SRC_PATH)
	  include_directories(${LLVM_SRC_PATH}/include)
	  message(STATUS "Looking for headers in: ${LLVM_SRC_PATH}/include")
	else()
	  include_directories(${LLVM_INCLUDE_DIRS})
	  message(STATUS "Looking for headers in: ${LLVM_INCLUDE_DIRS}")
	endif()

	if (LLVM_BUILD_PATH)
	  link_directories(${LLVM_BUILD_PATH}/lib)
	  message(STATUS "Looking for libraries in: ${LLVM_BUILD_PATH}/lib")
	else()
	  link_directories(${LLVM_LIBRARY_DIRS})
	  message(STATUS "Looking for libraries in: ${LLVM_LIBRARY_DIRS}")
	endif(LLVM_BUILD_PATH)

	add_definitions(${LLVM_DEFINITIONS})

	llvm_map_components_to_libnames(llvm_libs support core irreader)

	add_definitions(-DHAVE_LLVM)
endif(LLVM_DG)

if (ENABLE_POSTDOM)
	# need CFG for post-dom
	if (NOT ENABLE_CFG)
		message(STATUS "Enabling CFG due to post-dom edges (that are enabled)")
	endif()

	set(ENABLE_CFG ON)
	add_definitions(-DENABLE_POSTDOM)
endif()

if (ENABLE_CFG)
	add_definitions(-DENABLE_CFG)
endif()

if (ENABLE_DEBUG)
	add_definitions(-DDEBUG_ENABLED)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -frtti -fno-rtti -std=c++11 -g -O0")
else ()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -frtti -fno-rtti -std=c++11 -O2")
endif ()

add_subdirectory(src)
add_subdirectory(tests EXCLUDE_FROM_ALL)
add_subdirectory(tools)
