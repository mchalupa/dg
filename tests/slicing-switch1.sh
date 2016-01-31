#!/bin/bash

TESTS_DIR=`dirname $0`
source "$TESTS_DIR/test-runner.sh"

run_test "sources/switch1.c"
