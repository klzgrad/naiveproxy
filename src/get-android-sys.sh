#!/bin/sh
set -ex

. ./get-sysroot.sh

if [ "$WITH_ANDROID_IMG" -a ! -d out/sysroot-build/android/"$WITH_ANDROID_IMG"/system ]; then
  curl -O https://dl.google.com/android/repository/sys-img/android/$WITH_ANDROID_IMG.zip
  mkdir -p $WITH_ANDROID_IMG/mount
  unzip $WITH_ANDROID_IMG.zip '*/system.img' -d $WITH_ANDROID_IMG
  sudo mount $WITH_ANDROID_IMG/*/system.img $WITH_ANDROID_IMG/mount
  rootfs=out/sysroot-build/android/$WITH_ANDROID_IMG
  mkdir -p $rootfs/system/bin $rootfs/system/etc
  cp $WITH_ANDROID_IMG/mount/bin/linker* $rootfs/system/bin
  cp $WITH_ANDROID_IMG/mount/etc/hosts $rootfs/system/etc
  cp -r $WITH_ANDROID_IMG/mount/lib* $rootfs/system
  sudo umount $WITH_ANDROID_IMG/mount
  rm -rf $WITH_ANDROID_IMG $WITH_ANDROID_IMG.zip
fi
