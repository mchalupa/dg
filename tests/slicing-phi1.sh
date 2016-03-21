#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

run_test "sources/phi1.c"

echo -e "\nwithout PHI nodes it's OK\n"

set_environment

CODE="$TESTS_DIR/sources/phi1.c"
NAME=${CODE%.*}
BCFILE="$NAME.bc"

# now run the test with pure SSA form where
# are phi nodes
PHIBC="$NAME-phi.bc"
opt -mem2reg "$BCFILE" -o "$PHIBC"

SLICEDFILE2="$NAME-phi.sliced"
llvm-slicer $DG_TESTS_PTA -c test_assert "$PHIBC"

link_with_assert "$SLICEDFILE2" "$SLICEDFILE2.linked"

get_result "$SLICEDFILE2.linked"
