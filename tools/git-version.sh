#!/bin/sh

GIT_VERSION="`git rev-parse --short=8 HEAD`"

OLD_GIT_VERSION=
if [ -f "git-version.h" ]; then
	OLD_GIT_VERSION=`cat git-version.h | grep '#define GIT_VERSION' | cut -d '"' -f 2`
fi

if [ -z "$OLD_GIT_VERSION" -o "$OLD_GIT_VERSION" != "$GIT_VERSION" ]; then
	echo "#ifndef _DG_GIT_VERSION_" > "git-version.h"
	echo "#define _DG_GIT_VERSION_" >> "git-version.h"
	echo " #define GIT_VERSION \"$GIT_VERSION\"" >> "git-version.h"
	echo "#endif // _DG_GIT_VERSION_" >> "git-version.h"
fi

