#!/bin/sh

set -ex

script_dir=$(dirname "$PWD/$0")
PYTHON="$(which python3 2>/dev/null || which python 2>/dev/null)"

[ "$1" ] || exit 1
naive="$PWD/$1"

. ./get-sysroot.sh

if [ "$WITH_ANDROID_IMG" ]; then
  rootfs="$PWD/out/sysroot-build/android/$WITH_ANDROID_IMG"
elif [ "$WITH_SYSROOT" ]; then
  rootfs="$PWD/$WITH_SYSROOT"
fi

cd /tmp
"$PYTHON" "$script_dir"/basic.py --naive="$naive" --rootfs="$rootfs" --target_cpu="$target_cpu" --server_protocol=https
"$PYTHON" "$script_dir"/basic.py --naive="$naive" --rootfs="$rootfs" --target_cpu="$target_cpu" --server_protocol=http
