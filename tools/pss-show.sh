#!/bin/bash

#defult program
GUI=xdg-open

if which okular &>/dev/null; then
	GUI=okular
elif which evince &>/dev/null; then
	GUI=evince
fi

`dirname $0`/llvm-pss-dump -dot $@ | dot -Tpdf -o _pss.pdf
$GUI _pss.pdf
rm _pss.pdf
