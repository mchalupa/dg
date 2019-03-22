cmake_minimum_required(VERSION 2.8)
project(dg)

include(CTest)

# we need C++11 standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED on)

OPTION(LLVM_DG "Support for LLVM Dependency graph" ON)
OPTION(ENABLE_CFG "Add support for CFG edges to the graph" ON)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

if (LLVM_DG)
	# for llvm dg we need cfg and postdom edges
	if (NOT ENABLE_CFG)
		message(STATUS "Enabling CFG edges due to llvm dg")
	endif()

	set(ENABLE_CFG ON)

	find_package(LLVM REQUIRED CONFIG)

	message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
	message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
	message(STATUS "LLVM binaries: ${LLVM_TOOLS_BINARY_DIR}")

	set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_DIR}")
	include(LLVMConfig)
	include(AddLLVM)

	message(STATUS "LLVM include dir: ${LLVM_INCLUDE_DIRS}")
	message(STATUS "LLVM libraries dir: ${LLVM_LIBRARY_DIRS}")
	message(STATUS "LLVM definitions: ${LLVM_DEFINITIONS}")

	include_directories(${LLVM_INCLUDE_DIRS})
	link_directories(${LLVM_LIBRARY_DIRS})

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
	add_definitions(-DHAVE_LLVM)

	if (${LLVM_PACKAGE_VERSION} VERSION_GREATER "3.4")
		llvm_map_components_to_libnames(llvm_core core)
		llvm_map_components_to_libnames(llvm_irreader irreader)
		llvm_map_components_to_libnames(llvm_bitwriter bitwriter)
		llvm_map_components_to_libnames(llvm_analysis analysis)
		llvm_map_components_to_libnames(llvm_support support)
	else()
		llvm_map_components_to_libraries(llvm_core core)
		llvm_map_components_to_libraries(llvm_irreader irreader)
		llvm_map_components_to_libraries(llvm_bitwriter bitwriter)
		llvm_map_components_to_libraries(llvm_analysis analysis)
		llvm_map_components_to_libraries(llvm_support support)
	endif()
endif(LLVM_DG)

if (SVF_DIR)
	add_definitions(-DHAVE_SVF)
	set(SVF_LIBDIR ${SVF_DIR}/lib)

	if (SVF_INCLUDE)
	else()
		# suppose out-of-source build
		set(SVF_INCLUDE ${SVF_DIR}/../include)
	endif()

	set(SVF_LIBS LLVMSvf)

	include_directories(${SVF_INCLUDE})
	link_directories(${SVF_LIBDIR})

	message(STATUS "SVF dir: ${SVF_DIR}")
	message(STATUS "SVF libraries dir: ${SVF_LIBDIR}")
	message(STATUS "SVF include dir: ${SVF_INCLUDE}")
	message(STATUS "SVF libs: ${SVF_LIBS}")
endif(SVF_DIR)

if (ENABLE_CFG)
	add_definitions(-DENABLE_CFG)
endif()

message(STATUS "Using compiler: ${CMAKE_CXX_COMPILER}")


# Our CI does not work with fuzzing tests yet, make it optional by hand
option(TESTING_ENABLE_FUZZING "Enable fuzzing tests" off)
if (TESING_ENABLE_FUZZING)
	message(STATUS "Going to build fuzzing tests")
endif()
#if (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
	# FIXME: we should check for the version of clang
	# or rather just whether -fsanitize=fuzzer works
	#option(TESTING_ENABLE_FUZZING "Enable fuzzing tests" on)
	#message(STATUS "Will build fuzzing tests")
#else()
#	message(STATUS "Will NOT build fuzzing tests")
#endif()

# explicitly add -std=c++11 and -fno-rtti
# we have CMAKE_CXX_STANDARD, but for some reason it does not
# put the -std=c++11 or -std=gnu++11 to the flags
# on some systems. For the -fno-rtti: LLVM still got problems
# with turning RTTI off...
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -std=c++11 -Wall -Wextra")

if (USE_CLANG_SANITIZERS)
	message(WARNING "USE_CLANG_SANITIZERS has been renamed to USE_SANITIZERS")
	set(USE_SANITIZERS on)
endif()

if (USE_SANITIZERS)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined,address")
	add_definitions(-DUSING_SANITIZERS)
endif()

# Debug Release RelWithDebInfo MinSizeRel.
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_definitions(-DDEBUG_ENABLED)
	message(STATUS "Using compilation flags: ${CMAKE_CXX_FLAGS_DEBUG}")
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
	message(STATUS "Using compilation flags: ${CMAKE_CXX_FLAGS_RELEASE}")
elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
	message(STATUS
		"Using compilation flags: ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
elseif (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
	message(STATUS "Using compilation flags: ${CMAKE_CXX_FLAGS_MINSIZEREL}")
endif ()

message(STATUS "Additional compilation flags: ${CMAKE_CXX_FLAGS}")

include(GNUInstallDirs)
message(STATUS "CMAKE_INSTALL_LIBDIR: \"${CMAKE_INSTALL_LIBDIR}\"")
message(STATUS "CMAKE_INSTALL_INCLUDEDIR: \"${CMAKE_INSTALL_INCLUDEDIR}\"")

add_subdirectory(lib)
add_subdirectory(tools)
add_subdirectory(tests EXCLUDE_FROM_ALL)

install(DIRECTORY include/
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
