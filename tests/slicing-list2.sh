#!/bin/bash

# make bash exit on any failure
set -e

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

set_environment

CODE="$TESTS_DIR/sources/list2.c"
NAME=${CODE%.*}

BCFILE="$NAME.bc"
SLICEDFILE="$NAME.sliced"
CODEBCFILE="$NAME-nolib.bc"

LIB="$TESTS_DIR/sources/wl_list.c"
NAME=${LIB%.*}
LIBBCFILE="$NAME.bc"


clang -emit-llvm -c "$CODE" -o "$CODEBCFILE"
clang -emit-llvm -c "$LIB" -o "$LIBBCFILE"
llvm-link -o "$BCFILE" "$LIBBCFILE" "$CODEBCFILE"

llvm-slicer -c __assert_fail "$BCFILE"

# run the sliced code and check if it
# has everything to pass the assert in it
lli "$SLICEDFILE"
