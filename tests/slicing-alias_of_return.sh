#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

TESTS_CFLAGS="-O1 $TESTS_CFLAGS"
run_test "sources/alias_of_return.c"
