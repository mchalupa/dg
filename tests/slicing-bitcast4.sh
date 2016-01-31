#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

TESTS_CFLAGS="-std=gnu11"
run_test "sources/bitcast4.c"
