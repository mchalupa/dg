#!/bin/bash

TESTS_DIR=`dirname $0`

errmsg()
{

	echo "$1" 1>&2
	exit 1
}

get_llvm_builddir()
{
	# get cmake config line from cache
	LLVMDIR=`grep LLVM_DIR $TESTS_DIR/../CMakeCache.txt`

	# parse the LLVM_DIR:PATH=path_to_llvm_cmake_files
	echo $LLVMDIR | sed 's@.*PATH=\(.*\)/share/llvm/cmake@\1@'
}

set_environment()
{
	LLVMDIR=`get_llvm_builddir`
	LLVMBIN="$LLVMDIR/bin"
	TOOLSDIR="`readlink -f $TESTS_DIR/../tools`"
	export PATH="$LLVMBIN":"$TOOLSDIR":$PATH

	# self-test
	CLANGBIN=`which clang`
	if [ ! "`dirname $CLANGBIN`" != "$LLVMDIR" ]; then
		errmsg "Failed setting environment (llvm)"
	fi

	if [ ! which llvm-slicer &>/dev/null ]; then
		errmsg "Failed setting environment (tools)"
	fi
}
