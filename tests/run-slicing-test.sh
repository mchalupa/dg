#!/bin/bash

set -e

TEST=$1

echo "Test with PTA FI & DDA data-flow"
DG_TESTS_PTA=fi DG_TESTS_DDA=rd ./$TEST

echo "Test with PTA FS & DDA data-flow"
DG_TESTS_PTA=fs DG_TESTS_DDA=rd ./$TEST

echo "Test with PTA FI & DDA ssa"
DG_TESTS_PTA=fi DG_TESTS_DDA=ssa ./$TEST

echo "Test with PTA FS & DDA ssa"
DG_TESTS_PTA=fs DG_TESTS_DDA=ssa ./$TEST

echo "Test with PTA FS & DDA ssa & CD ntscd"
DG_TESTS_PTA=fs DG_TESTS_DDA=ssa DG_TESTS_CDA=ntscd ./$TEST

echo "Test with PTA FS & DDA ssa & CD ntscd"
DG_TESTS_PTA=fs DG_TESTS_DDA=ssa DG_TESTS_CDA=ntscd ./$TEST

