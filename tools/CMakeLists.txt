if (LLVM_DG)
if (${LLVM_PACKAGE_VERSION} VERSION_GREATER "3.4")
	llvm_map_components_to_libnames(llvm_irreader irreader)
	llvm_map_components_to_libnames(llvm_bitwriter bitwriter)
	llvm_map_components_to_libnames(llvm_analysis analysis)
else()
	llvm_map_components_to_libraries(llvm_irreader irreader)
	llvm_map_components_to_libraries(llvm_bitwriter bitwriter)
	llvm_map_components_to_libraries(llvm_analysis analysis)
endif()

	# generate a git-version.h with a HEAD commit hash tag
	# (if it changed)
	add_custom_target(
		gitversion ALL sh ${CMAKE_CURRENT_SOURCE_DIR}/git-version.sh
	)
	include_directories(${CMAKE_CURRENT_BINARY_DIR})

	add_executable(llvm-dg-dump llvm-dg-dump.cpp)
	target_link_libraries(llvm-dg-dump PRIVATE LLVMdg)
	target_link_libraries(llvm-dg-dump
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_bitwriter})

	add_executable(llvm-slicer llvm-slicer.cpp)
	target_link_libraries(llvm-slicer PRIVATE LLVMdg)
	target_link_libraries(llvm-slicer
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_bitwriter}
				PRIVATE ${llvm_analysis})
	add_dependencies(llvm-slicer gitversion)

	add_executable(llvm-ps-dump llvm-ps-dump.cpp)
	target_link_libraries(llvm-ps-dump PRIVATE LLVMdg)
	target_link_libraries(llvm-ps-dump
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis})

	add_executable(llvm-pta-compare llvm-pta-compare.cpp)
	target_link_libraries(llvm-pta-compare PRIVATE LLVMdg)
	target_link_libraries(llvm-pta-compare
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis})

	add_executable(llvm-rd-dump llvm-rd-dump.cpp)
	target_link_libraries(llvm-rd-dump PRIVATE LLVMdg)
	target_link_libraries(llvm-rd-dump
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis})

	add_executable(llvm-to-source llvm-to-source.cpp)
	target_link_libraries(llvm-to-source PRIVATE ${llvm_irreader})

	install(TARGETS llvm-dg-dump llvm-slicer
		RUNTIME DESTINATION bin)
endif (LLVM_DG)

include_directories(../src)

