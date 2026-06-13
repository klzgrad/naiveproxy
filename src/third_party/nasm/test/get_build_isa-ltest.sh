#!/bin/bash

: >> "$filelist"

if [ -d isa-l/.git ]; then
    cd isa-l
    git reset --hard
    xargs -r rm -f < "$filelist"
    make clean
else
    git clone https://github.com/intel/isa-l.git isa-l
    cd isa-l
fi
: > "$filelist"
make -f Makefile.unx -j
