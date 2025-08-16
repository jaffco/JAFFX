#!/bin/bash

git submodule update --recursive --init

START_DIR=$PWD
LIBDAISY_DIR=$PWD/libDaisy

echo "building libDaisy . . ."
cd "$LIBDAISY_DIR" ; make -s clean ; make -j -s
if [ $? -ne 0 ]
then
    echo "Failed to compile libDaisy."
    echo "Have you installed the Daisy Toolchain?"
    echo "See README.md"
    exit 1
fi
echo "done."