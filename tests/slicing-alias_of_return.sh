#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

export DG_TESTS_CFLAGS="-O1 $TESTS_CFLAGS"
run_test "sources/alias_of_return.c"
