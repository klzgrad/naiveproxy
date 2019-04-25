#!/bin/sh

set -ex

script_dir=$(dirname "$PWD/$0")

[ "$1" ] || exit 1
naive="$PWD/$1"

. ./get-sysroot.sh

if [ "$WITH_ANDROID_IMG" ]; then
  rootfs="$PWD/out/sysroot-build/android/$WITH_ANDROID_IMG"
elif [ "$WITH_SYSROOT" ]; then
  rootfs="$PWD/$WITH_SYSROOT"
fi

cd /tmp
python3 "$script_dir"/basic.py --naive="$naive" --rootfs="$rootfs" --target_cpu="$target_cpu" --server_protocol=https
python3 "$script_dir"/basic.py --naive="$naive" --rootfs="$rootfs" --target_cpu="$target_cpu" --server_protocol=http
