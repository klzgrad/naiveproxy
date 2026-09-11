#!/bin/bash

: >> "$filelist"

if [ -d isa-l_crypto/.git ]; then
    cd isa-l_crypto
    git reset --hard
    xargs -r rm -f < "$filelist"
    make clean
else
    git clone https://github.com/intel/isa-l_crypto.git isa-l_crypto
    cd isa-l_crypto
fi
: > "$filelist"
make -f Makefile.unx -j
