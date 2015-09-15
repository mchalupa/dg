#!/bin/bash

# make bash exit on any failure
set -e

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

set_environment

CODE="$TESTS_DIR/sources/phi2.c"
NAME=${CODE%.*}
BCFILE="$NAME.bc"
SLICEDFILE="$NAME.sliced"

clang -emit-llvm -c "$CODE" -o "$BCFILE"
llvm-slicer -c __assert_fail "$BCFILE"

# run the sliced code and check if it
# has everything to pass the assert in it
lli "$SLICEDFILE"

echo -e "\nwithout PHI nodes it's OK\n"

# now run the test with pure SSA form where
# are phi nodes
PHIBC="$NAME-phi.bc"
opt -mem2reg "$BCFILE" -o "$PHIBC"

SLICEDFILE="$NAME-phi.sliced"
llvm-slicer -c __assert_fail "$PHIBC"

lli "$SLICEDFILE"
