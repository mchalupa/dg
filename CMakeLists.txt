cmake_minimum_required(VERSION 2.8)
project(dg)

include(CTest)

include_directories( ${CMAKE_SOURCE_DIR}/src )

# we need C++11 standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED on)

# define DEBUG by default for now
OPTION(ENABLE_DEBUG "Enable debugging" ON)
OPTION(LLVM_DG "Support for LLVM Dependency graph" ON)
OPTION(ENABLE_CFG "Add support for CFG edges to the graph" ON)

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER}")

if (LLVM_DG)
	# for llvm dg we need cfg and postdom edges
	if (NOT ENABLE_CFG)
		message(STATUS "Enabling CFG edges due to llvm dg")
	endif()

	set(ENABLE_CFG ON)

	find_package(LLVM REQUIRED CONFIG)

	message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
	message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
	message(STATUS "LLVM binaires: ${LLVM_TOOLS_BINARY_DIR}")

	set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_DIR}")
	include(LLVMConfig)

	message(STATUS "LLVM include dir: ${LLVM_INCLUDE_DIRS}")
	message(STATUS "LLVM libraries dir: ${LLVM_LIBRARY_DIRS}")
	message(STATUS "LLVM definitions: ${LLVM_DEFINITIONS}")

	# if we were provided a path to custom sources
	# use that path
	if (LLVM_SRC_PATH)
	  include_directories(${LLVM_SRC_PATH}/include)
	  message(STATUS "Looking for headers in given: ${LLVM_SRC_PATH}/include")
	else()
	  include_directories(${LLVM_INCLUDE_DIRS})
	  message(STATUS "Looking for headers in: ${LLVM_INCLUDE_DIRS}")
	endif()

	# if we were provided a path to custom build directory
	# use that
	if (LLVM_BUILD_PATH)
	  link_directories(${LLVM_BUILD_PATH}/lib)

	  # llvm-config.h
	  include_directories("${LLVM_BUILD_PATH}/include")
	  message(STATUS "Looking for libraries in given: ${LLVM_BUILD_PATH}/lib")
	else()
	  link_directories(${LLVM_LIBRARY_DIRS})
	  message(STATUS "Looking for libraries in: ${LLVM_LIBRARY_DIRS}")
	endif(LLVM_BUILD_PATH)

	add_definitions(${LLVM_DEFINITIONS})

	if (${LLVM_PACKAGE_VERSION} VERSION_GREATER "3.4")
		llvm_map_components_to_libnames(llvm_libs support core
		                                irreader bitwriter analysis)
	else()
		llvm_map_components_to_libraries(llvm_libs support core
		                                 irreader bitwriter analysis)
	endif()

	add_definitions(-DHAVE_LLVM)
endif(LLVM_DG)

if (ENABLE_CFG)
	add_definitions(-DENABLE_CFG)
endif()

if (ENABLE_DEBUG)
	add_definitions(-DDEBUG_ENABLED)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -O0")
else ()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -O2")
endif ()

# generate a git-version.h with a HEAD commit hash tag
add_custom_target(
	gitversion ALL sh git-version.sh > git-version.h
)

add_subdirectory(src)
add_subdirectory(tests EXCLUDE_FROM_ALL)
add_subdirectory(tools)
