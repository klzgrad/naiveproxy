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

if [ "$major" -ge 24 ]; then
  tarball_suffix=zst
else
  tarball_suffix=xz
fi

if [ ! "$subtarget" ]; then
  subtarget=generic
fi

if [ "$subtarget" != generic -o "$major" -ge 22 ]; then
  SDK_PATH=openwrt-$path_suffix-$release-$target-${subtarget}_gcc-${gcc_ver}_${abi}.Linux-x86_64
else
  SDK_PATH=openwrt-$path_suffix-$release-${target}_gcc-${gcc_ver}_${abi}.Linux-x86_64
fi
SDK_URL=https://downloads.openwrt.org/releases/$release/targets/$target/$subtarget/$SDK_PATH.tar.$tarball_suffix
rm -rf $SDK_PATH
if [ $tarball_suffix = xz ]; then
  curl $SDK_URL | tar xJf -
elif [ $tarball_suffix = zst ]; then
  curl $SDK_URL | tar --zstd -xf -
fi

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

if [ "$arch" = "loongarch64" ]; then
  if grep HWCAP_LOONGARCH_LSX "$sysroot/include/bits/hwcap.h"; then
    echo "$sysroot/include/bits/hwcap.h" is already populated
    exit 1
  fi
  # https://www.openwall.com/lists/musl/2025/04/03/3
  cat >"$sysroot/include/bits/hwcap.h" <<EOF
/* The following must match the kernel's <asm/hwcap.h>.  */
/* HWCAP flags */
#define HWCAP_LOONGARCH_CPUCFG          (1 << 0)
#define HWCAP_LOONGARCH_LAM             (1 << 1)
#define HWCAP_LOONGARCH_UAL             (1 << 2)
#define HWCAP_LOONGARCH_FPU             (1 << 3)
#define HWCAP_LOONGARCH_LSX             (1 << 4)
#define HWCAP_LOONGARCH_LASX            (1 << 5)
#define HWCAP_LOONGARCH_CRC32           (1 << 6)
#define HWCAP_LOONGARCH_COMPLEX         (1 << 7)
#define HWCAP_LOONGARCH_CRYPTO          (1 << 8)
#define HWCAP_LOONGARCH_LVZ             (1 << 9)
#define HWCAP_LOONGARCH_LBT_X86         (1 << 10)
#define HWCAP_LOONGARCH_LBT_ARM         (1 << 11)
#define HWCAP_LOONGARCH_LBT_MIPS        (1 << 12)
#define HWCAP_LOONGARCH_PTW             (1 << 13)
EOF

  # https://www.openwall.com/lists/musl/2022/03/22/4
  # But this breaks C++.
  sed -i 's/extcontext\[\] /extcontext\[0\] /' "$sysroot/include/bits/signal.h"
fi
