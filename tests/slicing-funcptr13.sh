#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

set_environment

LIB="$TESTS_DIR/sources/get_ptr.c"
NAME=${LIB%.*}
LIBBCFILE="$NAME.bc"

CODE="$TESTS_DIR/sources/funcptr13.c"
NAME=${CODE%.*}
BCFILE="$NAME.bc"
SLICEDFILE="$NAME.sliced"
LINKEDFILE="$SLICEDFILE.linked"

# compile in.c out.bc
compile "$CODE" "$BCFILE"

if [ ! -z "$DG_TESTS_PTA" ]; then
	export DG_TESTS_PTA="-pta $DG_TESTS_PTA"
fi

# slice the code
llvm-slicer $DG_TESTS_PTA -c test_assert "$BCFILE" || exit 1

# compile additional definitions
clang -emit-llvm -c -Wall -Wextra "$LIB" -o "$LIBBCFILE"

llvm-link "$SLICEDFILE" "$LIBBCFILE" -o "$NAME-withdefs.bc"

# link assert to the code
link_with_assert "$NAME-withdefs.bc" "$LINKEDFILE"

# run the code and check result
get_result "$LINKEDFILE"
