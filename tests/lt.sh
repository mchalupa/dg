#!/bin/sh

dot -Tps last_test.dot > lt.ps || (echo "Something gone wrong"; exit 1;)

if which okular >/dev/null; then
	okular lt.ps
elif which evince >/dev/null; then
	evince lt.ps
elif xdg-open >/dev/null; then
	xdg-open lt.ps
fi
