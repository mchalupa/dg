#!/bin/sh

DIR=`dirname $0`

#defult program
GUI=xdg-open

if which okular &>/dev/null; then
	GUI=okular
elif which evince &>/dev/null; then
	GUI=evince
fi

$DIR/llvm-dg-dump $* > ldg-show-output.dot && dot -Tpdf ldg-show-output.dot -o ldg-show-output.pdf && $GUI ldg-show-output.pdf
rm ldg-show-output*
