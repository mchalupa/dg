#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

DG_TESTS_PTA=inv
run_test "sources/pta-inv-infinite-loop.c"
