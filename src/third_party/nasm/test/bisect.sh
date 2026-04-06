#!/bin/sh

# Usage:

# Make a test and a golden file, read ./performtest.pl --help

# cd nasm
# cp -r test somewhere (copy test dir out of the tree)
# git bisect start HEAD nasm-2.07 (where HEAD is bad and nasm-2.07 is good)
# git bisect run somewhere/test/bisect.sh br2148476 (what you want to test)

# Done


# Slow but sure
./autogen.sh
./configure
make

NASMDIR=$(pwd)
cd $(dirname "$0")
./performtest.pl "--nasm=$NASMDIR/nasm" "$1.asm" --verbose
