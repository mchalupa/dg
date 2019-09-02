#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

export DG_TESTS_SLICER_FLAGS=-threads
run_test "sources/threads1.c"
