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
	# or LLVM_DIR:UNINITIALIZED=path_to_llvm_cmake_files
	echo $LLVMDIR | sed 's@.*=\(.*\)/share/llvm/cmake@\1@'
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

compile()
{
	CODE="$1"
	BCFILE="$2"

	clang -emit-llvm -c -include "$TESTS_DIR/test_assert.h"\
		$TESTS_CFLAGS -Wall -Wextra "$CODE" -o "$BCFILE" \
		|| errmsg "Compilation failed"

	if [ "x$DG_TESTS_OPTIMIZE" != "x" ]; then
		opt $DG_TESTS_OPTIMIZE -o "$BCFILE-opt" "$BCFILE"\
		|| errmsg "Failed optimizing the file"

		# rename the optimize file to the output file
		mv "$BCFILE-opt" "$BCFILE"\
		|| errmsg "Failed renaming optimized file"
	fi
}

link_with_assert()
{
	FILE="$1"
	OUT="$2"
	shift
	shift

	TEST_ASSERT="$TESTS_DIR/test_assert.bc"

	clang -emit-llvm -c "$TESTS_DIR/test_assert.c" -o "$TEST_ASSERT" $@ \
		|| errmsg "Compilation of test_assert.c failed"
	llvm-link "$FILE" "$TEST_ASSERT" -o "$OUT" \
		|| errmsg "Linking with test_assert failed"
}

get_result()
{
	FILE="$1"
	OUTPUT="`lli "$FILE" 2>&1`"

	echo "$OUTPUT" | grep -q 'Assertion' || { echo "$OUTPUT"; errmsg "Assertion not called"; }
	# check explicitelly even for Assertion FAILED, because there can be more
	# asserts in the file and only some of them can fail
	echo "$OUTPUT" | grep -q 'Assertion FAILED' && { echo "$OUTPUT"; errmsg "Assertion failed"; }
	echo "$OUTPUT" | grep -q 'Assertion PASSED' || { echo "$OUTPUT"; errmsg "Assertion did not pass"; }

	echo "$OUTPUT"
}

run_test()
{
	TESTS_DIR=`dirname $0`

	set_environment

	CODE="$TESTS_DIR/$1"
	NAME=${CODE%.*}
	BCFILE="$NAME.bc"
	SLICEDFILE="$NAME.sliced"
	LINKEDFILE="$NAME.sliced.linked"

	# clean old files before running the test
	rm -f $BCFILE $SLICEDFILE $LINKEDFILE

	# compile in.c out.bc
	compile "$CODE" "$BCFILE"

	# slice the code
	if [ ! -z "$DG_TESTS_PTA" ]; then
		export DG_TESTS_PTA="-pta $DG_TESTS_PTA"
	fi

	if [ ! -z "$DG_TESTS_RDA" ]; then
		export DG_TESTS_RDA="-rda $DG_TESTS_RDA"
	fi

	llvm-slicer $DG_TESTS_RDA $DG_TESTS_PTA -c test_assert "$BCFILE"

	# link assert to the code
	link_with_assert "$SLICEDFILE" "$LINKEDFILE"

	# run the code and check result
	get_result "$LINKEDFILE"
}

