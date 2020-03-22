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
				PRIVATE dgllvmdg
				PRIVATE ${SVF_LIBS}
				PRIVATE ${llvm_transformutils}
				PRIVATE ${llvm_support}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_bitwriter}
				PRIVATE ${llvm_core})

	add_library(dgllvmslicer SHARED
		    llvm-slicer-opts.cpp llvm-slicer-opts.h
		    llvm-slicer-utils.cpp llvm-slicer-utils.h
		    llvm-slicer.h)
	#target_link_libraries(dgllvmslicer PUBLIC dgllvmdg)
	add_dependencies(dgllvmslicer gitversion)

	add_executable(llvm-slicer llvm-slicer.cpp llvm-slicer-crit.cpp)
	target_link_libraries(llvm-slicer PRIVATE dgllvmslicer
					  PRIVATE dgllvmdg
					  PRIVATE ${SVF_LIBS})

	add_executable(llvm-sdg-dump llvm-sdg-dump.cpp)
	target_link_libraries(llvm-sdg-dump PRIVATE dgllvmslicer
					    PRIVATE dgllvmsdg
					    PRIVATE ${SVF_LIBS}
					    PRIVATE ${llvm_support}
					    PRIVATE ${llvm_analysis}
					    PRIVATE ${llvm_irreader}
					    PRIVATE ${llvm_core}
					    )

	target_link_libraries(llvm-slicer
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_bitwriter}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support}
				PRIVATE ${llvm_transformutils}
				PRIVATE ${llvm_core})
	add_dependencies(llvm-slicer gitversion)

	add_executable(llvm-pta-dump llvm-pta-dump.cpp)
	target_link_libraries(llvm-pta-dump PRIVATE dgllvmpta)
	target_link_libraries(llvm-pta-dump
				PRIVATE ${llvm_core}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support})

	add_executable(llvm-pta-ben llvm-pta-ben.cpp)
	target_link_libraries(llvm-pta-ben PRIVATE dgllvmpta)
	target_link_libraries(llvm-pta-ben
				PRIVATE ${llvm_core}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support})

	add_executable(llvm-pta-compare llvm-pta-compare.cpp)
	target_link_libraries(llvm-pta-compare PRIVATE dgllvmpta)
	target_link_libraries(llvm-pta-compare
				PRIVATE ${llvm_core}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support})

	add_executable(llvm-dda-dump llvm-dda-dump.cpp)
	target_link_libraries(llvm-dda-dump PRIVATE dgllvmdda)
	target_link_libraries(llvm-dda-dump
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
        target_link_libraries(llvm-thread-regions-dump PRIVATE dgllvmthreadregions
                                                       PRIVATE ${llvm_core}
                                                       PRIVATE ${llvm_irreader}
                                                       PRIVATE ${llvm_support})

        add_executable(llvm-ntscd-dump llvm-ntscd-dump.cpp)
        target_link_libraries(llvm-ntscd-dump PRIVATE dgllvmcda
                                              PRIVATE ${llvm_core}
                                              PRIVATE ${llvm_irreader}
                                              PRIVATE ${llvm_analysis}
                                              PRIVATE ${llvm_support})

	install(TARGETS llvm-dg-dump llvm-slicer
		RUNTIME DESTINATION bin)

	install(TARGETS dgllvmslicer
		LIBRARY DESTINATION lib)

	install(FILES llvm-slicer.h llvm-slicer-opts.h llvm-slicer-utils.h
		DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dg/tools)

	# copy also the wrapper scripts
	install(FILES pta-show llvmdda-dump llvmdg-show llvm-slicer-c
		DESTINATION bin)
endif (LLVM_DG)

include_directories(../src)

