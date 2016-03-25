#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

set_environment

LIB="$TESTS_DIR/sources/wl_list.c"
NAME=${LIB%.*}
LIBBCFILE="$NAME.bc"

CODE="$TESTS_DIR/sources/list4.c"
NAME=${CODE%.*}
BCFILE="$NAME.bc"
SLICEDFILE="$NAME-withdefs.sliced"
LINKEDFILE="$SLICEDFILE.linked"

# compile in.c out.bc
compile "$CODE" "$BCFILE"

# compile additional definitions
clang -emit-llvm -c -Wall -Wextra "$LIB" -o "$LIBBCFILE"

llvm-link "$BCFILE" "$LIBBCFILE" -o "$NAME-withdefs.bc"

if [ ! -z "$DG_TESTS_PTA" ]; then
	export DG_TESTS_PTA="-pta $DG_TESTS_PTA"
fi

# slice the code
llvm-slicer $DG_TESTS_PTA -c test_assert "$NAME-withdefs.bc" || exit 1

# link assert to the code
link_with_assert "$SLICEDFILE" "$LINKEDFILE"

# run the code and check result
get_result "$LINKEDFILE"
