#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

set_environment

LIB="$TESTS_DIR/sources/wl_list.c"
NAME=${LIB%.*}
LIBBCFILE="$NAME.bc"

CODE="$TESTS_DIR/sources/list1.c"
NAME=${CODE%.*}
BCFILE="$NAME.bc"
SLICEDFILE="$NAME-withdefs.sliced"
LINKEDFILE="$SLICEDFILE.linked"

# compile in.c out.bc
compile "$CODE" "$BCFILE"

# compile additional definitions
clang -emit-llvm -c -Wall -Wextra "$LIB" -o "$LIBBCFILE"

llvm-link "$BCFILE" "$LIBBCFILE" -o "$NAME-withdefs.bc"

# slice the code
llvm-slicer -c test_assert "$NAME-withdefs.bc"

# link assert to the code
link_with_assert "$SLICEDFILE" "$LINKEDFILE"

# run the code and check result
get_result "$LINKEDFILE"
