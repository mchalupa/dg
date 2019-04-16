#!/bin/bash

set -e

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

set_environment

LIB="$TESTS_DIR/sources/unknownptrfoo2.c"
NAME=${LIB%.*}
LIBBCFILE="$NAME.bc"

CODE="$TESTS_DIR/sources/unknownptr7.c"
NAME=${CODE%.*}
BCFILE="$NAME.bc"
SLICEDFILE="$NAME.sliced"
LINKEDFILE="$SLICEDFILE.linked"

# compile in.c out.bc
compile "$CODE" "$BCFILE"

slice "$BCFILE" "$SLICEDFILE" || exit 1

# compile and link additional definitions
clang -emit-llvm -c -Wall -Wextra "$LIB" -o "$LIBBCFILE"
llvm-link "$SLICEDFILE" "$LIBBCFILE" -o "$SLICEDFILE-withdefs.bc"

# link assert to the code
link_with_assert "$SLICEDFILE-withdefs.bc" "$LINKEDFILE"

# run the code and check result
get_result "$LINKEDFILE" || exit 1
