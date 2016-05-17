#!/bin/bash

#defult program
GUI=xdg-open

if which okular &>/dev/null; then
	GUI=okular
elif which evince &>/dev/null; then
	GUI=evince
fi

`dirname $0`/llvm-rd-dump -dot $@ > _rd.dot && dot -Tpdf -o _rd.pdf _rd.dot && $GUI _rd.pdf
rm _rd.pdf _rd.dot
