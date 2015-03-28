#!/bin/sh

DIR=`dirname $0`

#defult program
GUI=xdg-open

if which okular &>/dev/null; then
	GUI=okular
elif which evince &>/dev/null; then
	GUI=evince
fi

$DIR/llvm-dg-dump $* > ldg-show-output.dot && dot -Tps ldg-show-output.dot > ldg-show-output.ps && $GUI ldg-show-output.ps
rm ldg-show-output*
