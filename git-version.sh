#!/bin/sh

GIT_VERSION=`git rev-parse --short=8 HEAD`
echo "#ifndef _DG_GIT_VERSION_"
echo "#define _DG_GIT_VERSION_"
echo " #define GIT_VERSION \"$GIT_VERSION\""
echo "#endif // _DG_GIT_VERSION_"
