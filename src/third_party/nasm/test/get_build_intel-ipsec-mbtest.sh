#!/bin/bash

: >> "$filelist"

if [ -d intel-ipsec-mb/.git ]; then
    cd intel-ipsec-mb
    git reset --hard
    xargs -r rm -f < "$filelist"
    rm -rf build
else
    git clone https://github.com/intel/intel-ipsec-mb.git
    cd intel-ipsec-mb
fi

: > "$filelist"
mkdir -p build
cd build
cmake ..
cmake --build . --parallel
