#!/bin/bash

# make bash exit on any failure
set -e

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

run_test "sources/phi1.c"

echo -e "\nwithout PHI nodes it's OK\n"

set_environment

CODE="$TESTS_DIR/sources/phi2.c"
NAME=${CODE%.*}
BCFILE="$NAME.bc"
SLICEDFILE="$NAME.sliced"

# now run the test with pure SSA form where
# are phi nodes
PHIBC="$NAME-phi.bc"
opt -mem2reg "$BCFILE" -o "$PHIBC"

SLICEDFILE="$NAME-phi.sliced"
llvm-slicer -c test_assert "$PHIBC"
link_with_assert "$PHIBC" "$PHIBC.linked"

get_result "$PHIBC.linked"
