#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

set_environment

LIB="$TESTS_DIR/sources/wl_list.c"
NAME=${LIB%.*}
LIBBCFILE="$NAME.bc"

# This tests works with list1.c source.
# The difference is that here we link the
# definitions after slicing, whereas in the list1
# test we link the definitions before slicing
CODE="$TESTS_DIR/sources/list1.c"
NAME=${CODE%.*}
BCFILE="$NAME.bc"
SLICEDFILE="$NAME.sliced"
LINKEDFILE="$SLICEDFILE.linked"

# compile in.c out.bc
compile "$CODE" "$BCFILE"

# compile additional definitions
clang -emit-llvm -c -Wall -Wextra "$LIB" -o "$LIBBCFILE"

if [ ! -z "$DG_TESTS_PTA" ]; then
	export DG_TESTS_PTA="-pta $DG_TESTS_PTA"
fi

# slice the code without definitions,
# the definitions will be added afterwards
llvm-slicer  $DG_TESTS_PTA -c test_assert "$NAME.bc" || exit 1

llvm-link "$SLICEDFILE" "$LIBBCFILE" -o "$NAME-withdefs.sliced"

# link assert to the code
link_with_assert "$NAME-withdefs.sliced" "$LINKEDFILE"

# run the code and check result
get_result "$LINKEDFILE"
