#!/bin/bash

DIR=`dirname $0`
cp $1 _code.c

clang -emit-llvm -c -g _code.c -o _code.bc

shift 1
$DIR/llvm-slicer -c $@ _code.bc

$DIR/llvm-to-source.py _code.sliced > _code.html && firefox _code.html
rm -rf _code.*

