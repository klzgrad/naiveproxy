#!/bin/sh

set -ex

arch=$OPENWRT_ARCH
release=$OPENWRT_RELEASE
gcc_ver=$OPENWRT_GCC

case "$arch" in
  mipsel_24kc) target=ramips subtarget=rt305x;;
  *) exit 1;;
esac

sysroot=$PWD/out/sysroot-build/openwrt/$release/$arch
if [ -d $sysroot/lib ]; then
  exit 0
fi
mkdir -p $sysroot

SDK_PATH=openwrt-sdk-$release-$target-${subtarget}_${gcc_ver}_musl.Linux-x86_64
SDK_URL=https://downloads.openwrt.org/releases/$release/targets/$target/$subtarget/$SDK_PATH.tar.xz
rm -rf $SDK_PATH
curl $SDK_URL | tar xJf -
cd $SDK_PATH
./scripts/feeds update base packages
./scripts/feeds install libnss
cp ../$target-$subtarget.config .config
yes | make oldconfig
make
full_root=staging_dir/toolchain-${arch}_${gcc_ver}_musl
cp -r staging_dir/target-${arch}_musl/usr $full_root
echo '
./include
./lib/*.o
./lib/gcc/*/libgcc.a
./lib/libatomic.so*
./lib/libc.so
./lib/libdl.a
./lib/ld-*
./lib/libgcc_s.*
./lib/libm.a
./lib/libpthread.a
./lib/libresolv.a
./lib/librt.a
./usr
' >include.txt
tar cf - -C $full_root . | tar xf - -C $sysroot --wildcards --wildcards-match-slash -T include.txt
rm include.txt
