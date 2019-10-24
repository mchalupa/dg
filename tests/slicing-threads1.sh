#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

if [ "$DG_TESTS_PTA" != "fi" ]; then
	echo "Unsupported PTA for threads";
	exit 0
fi
export DG_TESTS_SLICER_FLAGS=-threads
run_test "sources/threads1.c"
