#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

run_test "sources/llvmmemcpy2.c"
