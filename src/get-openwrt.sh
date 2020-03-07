#!/bin/sh

set -ex

eval "$OPENWRT_FLAGS"

sysroot=$PWD/out/sysroot-build/openwrt/$release/$arch
if [ -d $sysroot/lib ]; then
  exit 0
fi
mkdir -p $sysroot

SDK_PATH=openwrt-sdk-$release-$target-${subtarget}_gcc-${gcc_ver}_musl.Linux-x86_64
SDK_URL=https://downloads.openwrt.org/releases/$release/targets/$target/$subtarget/$SDK_PATH.tar.xz
rm -rf $SDK_PATH
curl $SDK_URL | tar xJf -
cd $SDK_PATH
./scripts/feeds update base packages
./scripts/feeds install libnss
make defconfig
for flag in ALL_NONSHARED ALL_KMODS ALL SIGNED_PACKAGES; do
  sed -i "s/CONFIG_$flag=y/# CONFIG_$flag is not set/" .config
done
make oldconfig
make
full_root=staging_dir/toolchain-*_gcc-${gcc_ver}_musl
cp -r staging_dir/target-*_musl/usr $full_root
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
*.ld.bin
' >include.txt
tar cf - -C $full_root --hard-dereference . | tar xf - -C $sysroot --wildcards --wildcards-match-slash -T include.txt
rm include.txt
cd $sysroot/*-openwrt-linux-musl/bin
mv .ld.bin ld
