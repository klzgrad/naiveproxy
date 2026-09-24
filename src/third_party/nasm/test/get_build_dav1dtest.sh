#!/bin/bash

: >> "$filelist"

if [ -d dav1d/.git ]; then
    cd dav1d
    git reset --hard
    cd build
    xargs -r rm -f < "$filelist"
    ninja clean
else
    git clone https://code.videolan.org/videolan/dav1d.git dav1d
    mkdir -p dav1d/build
    cd dav1d/build
    meson setup ..
fi
: > "$filelist"
#ncpus=$(ls -1 /sys/bus/cpu/devices | wc -l)
ninja -v
