#!/bin/bash

# make bash exit on any failure
set -e

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

clang -emit-llvm -c "$CODE" -o "$BCFILE"
clang -emit-llvm -c "$LIBCODE" -o "$LIBBCFILE"

llvm-slicer -c __assert_fail "$BCFILE"

# link sliced code with foo definition,
# so that we can run the code
RUNBC="$NAME.linked"
llvm-link "$SLICEDFILE" "$LIBBCFILE" -o "$RUNBC"

# run the sliced code and check if it
# has everything to pass the assert in it
lli "$RUNBC"
