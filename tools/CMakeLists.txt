include_directories(${CMAKE_SOURCE_DIR}/include)
# these tools can access the private headers
include_directories(${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/tools/include)

if (LLVM_DG)

    # generate a git-version.h with a HEAD commit hash tag (if it changed)
    find_package(Git)
    if (NOT Git_FOUND OR NOT EXISTS ${CMAKE_SOURCE_DIR}/.git AND NOT GIT_VERSION)
        set(GIT_VERSION "unknown")
    else()
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        OUTPUT_VARIABLE NEW_GIT_VERSION)
        if (NOT "${GIT_VERSION}" STREQUAL "${NEW_GIT_VERSION}")
            set(GIT_VERSION ${NEW_GIT_VERSION})
        endif()
    endif()

    set(GIT_VERSION ${GIT_VERSION} CACHE STRING "Git version hash" FORCE)
    configure_file(git-version.h.in git-version.h @ONLY)
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
		    llvm-slicer-opts.cpp include/dg/tools/llvm-slicer-opts.h
		    llvm-slicer-utils.cpp include/dg/tools/llvm-slicer-utils.h
		    llvm-slicer-crit.cpp
		    include/dg/tools/llvm-slicer.h)
	target_link_libraries(dgllvmslicer PUBLIC dgllvmdg)

	add_executable(llvm-slicer llvm-slicer.cpp)
	target_link_libraries(llvm-slicer PRIVATE dgllvmslicer
					  PRIVATE dgllvmdg
					  PRIVATE ${SVF_LIBS})

	add_executable(llvm-sdg-dump llvm-sdg-dump.cpp)
	target_link_libraries(llvm-sdg-dump PRIVATE dgllvmslicer
					    PRIVATE dgllvmsdg
					    PRIVATE ${SVF_LIBS}
					    PRIVATE ${llvm_support}
					    PRIVATE ${llvm_irreader}
					    PRIVATE ${llvm_core}
					    )

	add_executable(llvm-cg-dump llvm-cg-dump.cpp)
	target_link_libraries(llvm-cg-dump PRIVATE dgllvmslicer
					    PRIVATE dgllvmpta
					    PRIVATE ${llvm_support}
					    PRIVATE ${llvm_analysis}
					    PRIVATE ${llvm_irreader}
					    PRIVATE ${llvm_core}
					    )
if (HAVE_SVF)
	target_link_libraries(llvm-cg-dump
                                           PRIVATE ${SVF_LIBS}
                                           PRIVATE ${llvm_bitwriter}
                                           PRIVATE ${llvm_transformutils})
endif (HAVE_SVF)

	add_executable(llvm-cda-dump llvm-cda-dump.cpp llvm-slicer-metadata.cpp)
	target_link_libraries(llvm-cda-dump PRIVATE dgllvmslicer
                                            PRIVATE dganalysis
                                            PRIVATE dgllvmcda
					    PUBLIC ${llvm_analysis}
					    PRIVATE ${llvm_irreader}
					    PRIVATE ${llvm_core}
					    )

	add_executable(llvm-cda-bench llvm-cda-bench.cpp)
	target_link_libraries(llvm-cda-bench PRIVATE dgllvmslicer
                                            PRIVATE dganalysis
                                            PRIVATE dgllvmcda
					    PUBLIC ${llvm_analysis}
					    PRIVATE ${llvm_irreader}
					    PRIVATE ${llvm_core}
					    )

	add_executable(llvm-cda-stress llvm-cda-stress.cpp)
	target_link_libraries(llvm-cda-stress PRIVATE dgllvmslicer
                                            PRIVATE dganalysis
                                            PRIVATE dgcda
					    PRIVATE ${llvm_support}
					    PRIVATE ${llvm_core}
					    )

	target_link_libraries(llvm-slicer
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_bitwriter}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support}
				PRIVATE ${llvm_transformutils}
				PRIVATE ${llvm_core})

	add_executable(llvm-pta-dump llvm-pta-dump.cpp llvm-slicer-metadata.cpp)
	target_link_libraries(llvm-pta-dump PRIVATE dgllvmpta
                                            PRIVATE dgllvmslicer)
if (HAVE_SVF)
	target_link_libraries(llvm-pta-dump PRIVATE ${SVF_LIBS}
                                            PRIVATE ${llvm_bitwriter}
                                            PRIVATE ${llvm_transformutils})
endif (HAVE_SVF)


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
                                PRIVATE dgllvmslicer
				PRIVATE ${llvm_core}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support})
if (HAVE_SVF)
	target_link_libraries(llvm-pta-compare PRIVATE ${SVF_LIBS}
                                               PRIVATE ${llvm_bitwriter}
                                               PRIVATE ${llvm_transformutils})
endif (HAVE_SVF)



	add_executable(llvm-dda-dump llvm-dda-dump.cpp llvm-slicer-metadata.cpp)
	target_link_libraries(llvm-dda-dump PRIVATE dgllvmdda)
	target_link_libraries(llvm-dda-dump
                                PRIVATE dgllvmslicer
				PRIVATE ${llvm_core}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support})

	add_executable(llvm-vr-dump llvm-vr-dump.cpp)
	target_link_libraries(llvm-vr-dump
				# dynamic LLVM
				PRIVATE ${llvm}
				# static LLVM
				PRIVATE ${llvm_core}
				PRIVATE ${llvm_irreader}
				PRIVATE ${llvm_analysis}
				PRIVATE ${llvm_support})

	add_executable(llvm-to-source llvm-to-source.cpp)
	target_link_libraries(llvm-to-source
				# dynamic LLVM
				PRIVATE ${llvm}
				# static LLVM
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

	install(TARGETS llvm-slicer
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

	install(TARGETS dgllvmslicer
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

        install(DIRECTORY include/
	        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif (LLVM_DG)
