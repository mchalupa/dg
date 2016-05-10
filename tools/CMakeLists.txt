if (LLVM_DG)
	set(LLVM_LINK_COMPONENTS core engine asmparser bitreader support)

	add_executable(llvm-dg-dump llvm-dg-dump.cpp)
	target_link_libraries(llvm-dg-dump LLVMdg)

	add_executable(llvm-slicer llvm-slicer.cpp)
	target_link_libraries(llvm-slicer LLVMdg)

	add_executable(llvm-pss-dump llvm-pss-dump.cpp)
	target_link_libraries(llvm-pss-dump LLVMdg)

	add_executable(llvm-pss-compare llvm-pss-compare.cpp)
	target_link_libraries(llvm-pss-compare LLVMdg)

	add_executable(llvm-rd-dump llvm-rd-dump.cpp)
	target_link_libraries(llvm-rd-dump LLVMdg)

	add_executable(llvm-to-source llvm-to-source.cpp)
	target_link_libraries(llvm-to-source ${llvm_libs})

	install(TARGETS llvm-dg-dump llvm-slicer
		RUNTIME DESTINATION bin)
endif (LLVM_DG)

include_directories('../src')

if (ENABLE_DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
endif ()
