#!/bin/bash

: >> "$filelist"

if [ -d ffmpeg/.git ]; then
    cd ffmpeg
    git reset --hard
    xargs -r rm -f < "$filelist"
else
    git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg
    cd ffmpeg
fi
: > "$filelist"
./configure --disable-stripping
ncpus=$(ls -1 /sys/bus/cpu/devices | wc -l)
make -j${ncpus}
