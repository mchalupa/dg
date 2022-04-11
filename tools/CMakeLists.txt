if (LLVM_DG)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

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
    target_link_libraries(llvm-dg-dump PRIVATE dgllvmslicer
                                       PRIVATE ${llvm_bitwriter}
                                       PRIVATE ${llvm_irreader})
    if(HAVE_SVF)
        target_link_libraries(llvm-dg-dump PRIVATE ${SVF_LIBS}
                                           PRIVATE ${llvm_transformutils})
    endif()

	add_library(dgllvmslicer SHARED
		    llvm-slicer-metadata.cpp
		    llvm-slicer-opts.cpp
		    llvm-slicer-utils.cpp
		    llvm-slicer-preprocess.cpp
		    llvm-slicer-crit.cpp)
	target_link_libraries(dgllvmslicer PUBLIC dgllvmdg)

	add_executable(llvm-slicer llvm-slicer.cpp)
	target_link_libraries(llvm-slicer PRIVATE dgllvmslicer
					  PRIVATE ${llvm_irreader}
					  PRIVATE ${llvm_bitwriter})
    if(HAVE_SVF)
        target_link_libraries(llvm-slicer PRIVATE ${SVF_LIBS}
                                          PRIVATE ${llvm_transformutils})
    endif()

	add_executable(llvm-sdg-dump llvm-sdg-dump.cpp)
	target_link_libraries(llvm-sdg-dump PRIVATE dgllvmslicer
					    PRIVATE dgllvmsdg
					    PRIVATE ${SVF_LIBS}
					    PRIVATE ${llvm_irreader}
					    )

	add_executable(llvm-cg-dump llvm-cg-dump.cpp)
	target_link_libraries(llvm-cg-dump PRIVATE dgllvmslicer
					   PRIVATE dgllvmpta
					   PRIVATE ${llvm_irreader}
					    )
if (HAVE_SVF)
   target_link_libraries(llvm-cg-dump PRIVATE ${SVF_LIBS}
                                      PRIVATE ${llvm_bitwriter}
                                      PRIVATE ${llvm_transformutils})
endif (HAVE_SVF)

	add_executable(llvm-cda-dump llvm-cda-dump.cpp)
	target_link_libraries(llvm-cda-dump PRIVATE dgllvmslicer
                                            PRIVATE dgllvmcda
					    PRIVATE ${llvm_irreader}
					    )
if (HAVE_SVF)
   target_link_libraries(llvm-cda-dump PRIVATE ${SVF_LIBS}
                                      PRIVATE ${llvm_bitwriter}
                                      PRIVATE ${llvm_transformutils})
endif (HAVE_SVF)

	add_executable(llvm-cda-bench llvm-cda-bench.cpp)
	target_link_libraries(llvm-cda-bench PRIVATE dgllvmslicer
                                            PRIVATE dgllvmcda
					    PRIVATE ${llvm_irreader}
					    )

	add_executable(llvm-cda-stress llvm-cda-stress.cpp)
	target_link_libraries(llvm-cda-stress PRIVATE dgllvmslicer
                                            PRIVATE dgcda
					    PRIVATE ${llvm_irreader}
					    )

	add_executable(llvm-pta-dump llvm-pta-dump.cpp)
	target_link_libraries(llvm-pta-dump PRIVATE dgllvmpta
                                            PRIVATE dgllvmslicer)
if (HAVE_SVF)
    target_link_libraries(llvm-pta-dump PRIVATE ${SVF_LIBS}
                                        PRIVATE ${llvm_bitwriter}
                                        PRIVATE ${llvm_transformutils})
endif (HAVE_SVF)


	target_link_libraries(llvm-pta-dump
				PRIVATE ${llvm_irreader})

    add_executable(llvm-pta-ben llvm-pta-ben.cpp)
    target_link_libraries(llvm-pta-ben PRIVATE dgllvmslicer
                                       PRIVATE ${llvm_irreader})

	add_executable(llvm-pta-compare llvm-pta-compare.cpp)
	target_link_libraries(llvm-pta-compare PRIVATE dgllvmpta)
	target_link_libraries(llvm-pta-compare
                                PRIVATE dgllvmslicer
				PRIVATE ${llvm_irreader})
if (HAVE_SVF)
	target_link_libraries(llvm-pta-compare PRIVATE ${SVF_LIBS}
                                               PRIVATE ${llvm_bitwriter}
                                               PRIVATE ${llvm_transformutils})
endif (HAVE_SVF)



	add_executable(llvm-dda-dump llvm-dda-dump.cpp)
	target_link_libraries(llvm-dda-dump PRIVATE dgllvmdda)
	target_link_libraries(llvm-dda-dump
                                PRIVATE dgllvmslicer
				PRIVATE ${llvm_irreader})

	add_executable(llvm-vr-dump llvm-vr-dump.cpp)
	target_link_libraries(llvm-vr-dump
				# dynamic LLVM
				PRIVATE dgllvmvra
				PRIVATE ${llvm}
				# static LLVM
				PRIVATE ${llvm_irreader})

	add_executable(llvm-to-source llvm-to-source.cpp)
	target_link_libraries(llvm-to-source
				# dynamic LLVM
				PRIVATE ${llvm}
				# static LLVM
				PRIVATE ${llvm_irreader})

        add_executable(llvm-thread-regions-dump llvm-thread-regions-dump.cpp)
        target_link_libraries(llvm-thread-regions-dump PRIVATE dgllvmthreadregions
                                                       PRIVATE ${llvm_irreader})

        add_executable(llvm-ntscd-dump llvm-ntscd-dump.cpp)
        target_link_libraries(llvm-ntscd-dump PRIVATE dgllvmcda
	                                      PRIVATE ${llvm_analysis}
                                              PRIVATE ${llvm_irreader})

	install(TARGETS llvm-slicer
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

	install(TARGETS dgllvmslicer
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

        install(DIRECTORY include/
	        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif (LLVM_DG)
