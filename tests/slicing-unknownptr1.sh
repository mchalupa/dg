#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

set_environment

CODE="$TESTS_DIR/sources/unknownptr1.c"
NAME=${CODE%.*}
BCFILE="$NAME.bc"
SLICEDFILE="$NAME.sliced"

LIBCODE="$TESTS_DIR/sources/unknownptrfoo.c"
LIBNAME=${LIBCODE%.*}
LIBBCFILE="$LIBNAME.bc"

compile "$CODE" "$BCFILE"
clang -emit-llvm -c "$LIBCODE" -o "$LIBBCFILE"
clang -emit-llvm -c "$TESTS_DIR/test_assert.c" -o "$TESTS_DIR/test_assert.bc"

if [ ! -z "$DG_TESTS_PTA" ]; then
		export DG_TESTS_PTA="-pta $DG_TESTS_PTA"
fi

llvm-slicer $DG_TESTS_PTA -c test_assert "$BCFILE" || exit 1

# link sliced code with foo definition,
# so that we can run the code
RUNBC="$NAME.linked"
llvm-link "$SLICEDFILE" "$LIBBCFILE" "$TESTS_DIR/test_assert.bc" -o "$RUNBC"

# run the sliced code and check if it
# has everything to pass the assert in it
get_result "$RUNBC"
