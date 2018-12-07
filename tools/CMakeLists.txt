include_directories(${CMAKE_SOURCE_DIR}/include)
# these tools can access the private headers
include_directories(${CMAKE_SOURCE_DIR}/lib)

if (LLVM_DG)
	# generate a git-version.h with a HEAD commit hash tag
	# (if it changed)
	add_custom_target(
		gitversion ALL sh ${CMAKE_CURRENT_SOURCE_DIR}/git-version.sh
	)
	include_directories(${CMAKE_CURRENT_BINARY_DIR})

	add_executable(llvm-dg-dump llvm-dg-dump.cpp)
	target_link_libraries(llvm-dg-dump
				PRIVATE LLVMdg
				PRIVATE ${llvm_support}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_bitwriter}
				PRIVATE ${llvm_core})

	add_executable(llvm-slicer llvm-slicer.cpp
				   llvm-slicer-opts.cpp llvm-slicer-opts.h)
	target_link_libraries(llvm-slicer PRIVATE LLVMdg)
	target_link_libraries(llvm-slicer
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_bitwriter}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support}
				PRIVATE ${llvm_core})
	add_dependencies(llvm-slicer gitversion)

	add_executable(llvm-ps-dump llvm-ps-dump.cpp)
	target_link_libraries(llvm-ps-dump PRIVATE LLVMpta)
	target_link_libraries(llvm-ps-dump
				PRIVATE ${llvm_core}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support})

	add_executable(llvm-pta-compare llvm-pta-compare.cpp)
	target_link_libraries(llvm-pta-compare PRIVATE LLVMpta)
	target_link_libraries(llvm-pta-compare
				PRIVATE ${llvm_core}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support})

	add_executable(llvm-rd-dump llvm-rd-dump.cpp)
	target_link_libraries(llvm-rd-dump PRIVATE LLVMrd)
	target_link_libraries(llvm-rd-dump
				PRIVATE ${llvm_core}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support})

	add_executable(llvm-vr-dump llvm-vr-dump.cpp)
	target_link_libraries(llvm-vr-dump
				PRIVATE ${llvm_core}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support})

	add_executable(llvm-to-source llvm-to-source.cpp)
	target_link_libraries(llvm-to-source
				PRIVATE ${llvm_core}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_support})

        add_executable(llvm-thread-regions-dump llvm-thread-regions-dump.cpp)
        target_link_libraries(llvm-thread-regions-dump PRIVATE ThreadRegions)
        set_target_properties(llvm-thread-regions-dump
                PROPERTIES
                CXX_STANDARD 14)

	install(TARGETS llvm-dg-dump llvm-slicer
		RUNTIME DESTINATION bin)

	# copy also the wrapper scripts
	configure_file(ps-show ps-show COPYONLY)
	configure_file(rd-show rd-show COPYONLY)
	configure_file(llvmdg-show llvmdg-show COPYONLY)
endif (LLVM_DG)

include_directories(../src)

