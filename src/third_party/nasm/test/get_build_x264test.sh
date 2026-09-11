#!/bin/bash

: >> "$filelist"

if [ -d x264/.git ]; then
    cd x264
    git reset --hard
    xargs -r rm -f < "$filelist"
    make clean
else
    git clone https://code.videolan.org/videolan/x264.git x264
    cd x264
fi
: > "$filelist"
./configure
ncpus=$(ls -1 /sys/bus/cpu/devices | wc -l)
make -j${ncpus}
