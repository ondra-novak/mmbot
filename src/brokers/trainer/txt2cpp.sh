#!/bin/sh

cd `dirname $0`

mkdir -p generated

INPUTFILE=$1
VARIABLE_NAME=`echo -n $1 | tr '.' '_'`
OUTPUT_NAME=generated/$1.cpp

echo -n "const char *$VARIABLE_NAME=R\"txt2cpp(" > $OUTPUT_NAME
cat $INPUTFILE >> $OUTPUT_NAME
echo ")txt2cpp\";" >> $OUTPUT_NAME
