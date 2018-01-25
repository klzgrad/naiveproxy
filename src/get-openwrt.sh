#!/bin/sh

set -ex

eval "$OPENWRT_FLAGS"

sysroot=$PWD/out/sysroot-build/openwrt/$release/$arch
if [ -d $sysroot/lib ]; then
  exit 0
fi
mkdir -p $sysroot

case "$arch" in
arm_*) abi=musl_eabi;;
*) abi=musl;;
esac

major=${release%%.*}
if [ "$major" -ge 22 ]; then
  path_suffix=toolchain
else
  path_suffix=sdk
fi

if [ ! "$subtarget" ]; then
  subtarget=generic
fi

if [ "$subtarget" != generic -o "$major" -ge 22 ]; then
  SDK_PATH=openwrt-$path_suffix-$release-$target-${subtarget}_gcc-${gcc_ver}_${abi}.Linux-x86_64
else
  SDK_PATH=openwrt-$path_suffix-$release-${target}_gcc-${gcc_ver}_${abi}.Linux-x86_64
fi
SDK_URL=https://downloads.openwrt.org/releases/$release/targets/$target/$subtarget/$SDK_PATH.tar.xz
rm -rf $SDK_PATH
curl $SDK_URL | tar xJf -

full_root=toolchain-*_gcc-${gcc_ver}_${abi}

if [ "$major" -lt 22 ]; then
  mv $SDK_PATH/staging_dir/$full_root $SDK_PATH
fi

cd $SDK_PATH
cat >include.txt <<EOF
./include
./lib/*.o
./lib/gcc/*/libgcc.a
./lib/gcc/*/libgcc_eh.a
./lib/libatomic.so*
./lib/libatomic.a
./lib/libc.so
./lib/libc.a
./lib/libdl.a
./lib/ld-*
./lib/libgcc_s.*
./lib/libm.a
./lib/libpthread.a
./lib/libresolv.a
./lib/librt.a
./usr
EOF
tar cf - -C $full_root --hard-dereference . | tar xf - -C $sysroot --wildcards --wildcards-match-slash -T include.txt
rm -rf include.txt
cd ..
rm -rf $SDK_PATH

# LLVM does not accept muslgnueabi as the target triple environment
if [ -d "$sysroot/lib/gcc/arm-openwrt-linux-muslgnueabi" ]; then
  mv "$sysroot/lib/gcc/arm-openwrt-linux-muslgnueabi" "$sysroot/lib/gcc/arm-openwrt-linux-musleabi"
fi
