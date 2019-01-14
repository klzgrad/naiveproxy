#!/bin/sh
set -ex
cd src

unset EXTRA_FLAGS
unset OPENWRT_FLAGS
ccache -C

./get-clang.sh

for i in x64 x86 arm64 arm mipsel mips64el; do
  unset EXTRA_FLAGS
  unset OPENWRT_FLAGS
  export EXTRA_FLAGS="target_cpu=\"$i\""
  ./get-clang.sh
done

for i in x64 x86 arm64 arm; do
  unset EXTRA_FLAGS
  unset OPENWRT_FLAGS
  export EXTRA_FLAGS="target_cpu=\"$i\" target_os=\"android\""
  ./get-clang.sh
done

config_openwrt() {
  arch="$1"
  openwrt="$2"
  target_cpu="$3"
  extra="$4"
  export EXTRA_FLAGS="target_cpu=\"$target_cpu\" target_os=\"openwrt\" use_allocator=\"none\" use_allocator_shim=false $extra"
  export OPENWRT_FLAGS="arch=$arch release=19.07.7 gcc_ver=7.5.0 $openwrt"
  ./get-clang.sh
}

config_openwrt x86_64 'target=x86 subtarget=64' x64
config_openwrt x86 'target=x86 subtarget=generic' x86
config_openwrt aarch64_cortex-a53 'target=sunxi subtarget=cortexa53' arm64 'arm_version=0 arm_cpu="cortex-a53"'
config_openwrt aarch64_cortex-a72 'target=mvebu subtarget=cortexa72' arm64 'arm_version=0 arm_cpu="cortex-a72"'
config_openwrt aarch64_generic 'target=armvirt subtarget=64' arm64
config_openwrt arm_cortex-a5_vfpv4 'target=at91 subtarget=sama5' arm 'arm_version=0 arm_cpu="cortex-a5" arm_fpu="vfpv4" arm_float_abi="hard" arm_use_neon=false'
config_openwrt arm_cortex-a7_neon-vfpv4 'target=sunxi subtarget=cortexa7' arm 'arm_version=0 arm_cpu="cortex-a7" arm_fpu="neon-vfpv4" arm_float_abi="hard" arm_use_neon=true'
config_openwrt arm_cortex-a8_neon 'target=samsung subtarget=s5pv210' arm 'arm_version=0 arm_cpu="cortex-a8" arm_fpu="neon" arm_float_abi="hard" arm_use_neon=true'
config_openwrt arm_cortex-a8_vfpv3 'target=sunxi subtarget=cortexa8' arm 'arm_version=0 arm_cpu="cortex-a8" arm_fpu="vfpv3" arm_float_abi="hard" arm_use_neon=false'
config_openwrt arm_cortex-a9 'target=bcm53xx' arm 'arm_version=0 arm_cpu="cortex-a9" arm_float_abi="soft" arm_use_neon=false'
config_openwrt arm_cortex-a9_neon 'target=imx6' arm 'arm_version=0 arm_cpu="cortex-a9" arm_fpu="neon" arm_float_abi="hard" arm_use_neon=true'
config_openwrt arm_cortex-a9_vfpv3-d16 'target=tegra' arm 'arm_version=0 arm_cpu="cortex-a9" arm_fpu="vfpv3-d16" arm_float_abi="hard" arm_use_neon=false'
config_openwrt arm_cortex-a15_neon-vfpv4 'target=armvirt subtarget=32' arm 'arm_version=0 arm_cpu="cortex-a15" arm_fpu="neon-vfpv4" arm_float_abi="hard" arm_use_neon=true'
config_openwrt mipsel_24kc 'target=ramips subtarget=rt305x' mipsel 'mips_arch_variant="r2" mips_float_abi="soft" mips_tune="24kc" use_lld=false use_gold=false'
config_openwrt mipsel_74kc 'target=ramips subtarget=rt3883' mipsel 'mips_arch_variant="r2" mips_float_abi="soft" mips_tune="74kc" use_lld=false use_gold=false'
config_openwrt mipsel_mips32 'target=rb532' mipsel 'mips_arch_variant="r1" mips_float_abi="soft" use_lld=false use_gold=false'

rm -f /tmp/trace
inotifywait -m -r -o/tmp/trace --format '%w%f %e' . &
pid=$!

unset EXTRA_FLAGS
unset OPENWRT_FLAGS
./build.sh

for i in x64 x86 arm64 arm mipsel mips64el; do
  unset EXTRA_FLAGS
  unset OPENWRT_FLAGS
  export EXTRA_FLAGS="target_cpu=\"$i\""
  ./build.sh
done

for i in x64 x86 arm64 arm; do
  unset EXTRA_FLAGS
  unset OPENWRT_FLAGS
  export EXTRA_FLAGS="target_cpu=\"$i\" target_os=\"android\""
  ./build.sh
done

build_openwrt() {
  arch="$1"
  openwrt="$2"
  target_cpu="$3"
  extra="$4"
  export EXTRA_FLAGS="target_cpu=\"$target_cpu\" target_os=\"openwrt\" use_allocator=\"none\" use_allocator_shim=false $extra"
  export OPENWRT_FLAGS="arch=$arch release=19.07.7 gcc_ver=7.5.0 $openwrt"
  ./build.sh
}

build_openwrt x86_64 'target=x86 subtarget=64' x64
build_openwrt x86 'target=x86 subtarget=generic' x86
build_openwrt aarch64_cortex-a53 'target=sunxi subtarget=cortexa53' arm64 'arm_version=0 arm_cpu="cortex-a53"'
build_openwrt aarch64_cortex-a72 'target=mvebu subtarget=cortexa72' arm64 'arm_version=0 arm_cpu="cortex-a72"'
build_openwrt aarch64_generic 'target=armvirt subtarget=64' arm64
build_openwrt arm_cortex-a5_vfpv4 'target=at91 subtarget=sama5' arm 'arm_version=0 arm_cpu="cortex-a5" arm_fpu="vfpv4" arm_float_abi="hard" arm_use_neon=false'
build_openwrt arm_cortex-a7_neon-vfpv4 'target=sunxi subtarget=cortexa7' arm 'arm_version=0 arm_cpu="cortex-a7" arm_fpu="neon-vfpv4" arm_float_abi="hard" arm_use_neon=true'
build_openwrt arm_cortex-a8_neon 'target=samsung subtarget=s5pv210' arm 'arm_version=0 arm_cpu="cortex-a8" arm_fpu="neon" arm_float_abi="hard" arm_use_neon=true'
build_openwrt arm_cortex-a8_vfpv3 'target=sunxi subtarget=cortexa8' arm 'arm_version=0 arm_cpu="cortex-a8" arm_fpu="vfpv3" arm_float_abi="hard" arm_use_neon=false'
build_openwrt arm_cortex-a9 'target=bcm53xx' arm 'arm_version=0 arm_cpu="cortex-a9" arm_float_abi="soft" arm_use_neon=false'
build_openwrt arm_cortex-a9_neon 'target=imx6' arm 'arm_version=0 arm_cpu="cortex-a9" arm_fpu="neon" arm_float_abi="hard" arm_use_neon=true'
build_openwrt arm_cortex-a9_vfpv3-d16 'target=tegra' arm 'arm_version=0 arm_cpu="cortex-a9" arm_fpu="vfpv3-d16" arm_float_abi="hard" arm_use_neon=false'
build_openwrt arm_cortex-a15_neon-vfpv4 'target=armvirt subtarget=32' arm 'arm_version=0 arm_cpu="cortex-a15" arm_fpu="neon-vfpv4" arm_float_abi="hard" arm_use_neon=true'
build_openwrt mipsel_24kc 'target=ramips subtarget=rt305x' mipsel 'mips_arch_variant="r2" mips_float_abi="soft" mips_tune="24kc" use_lld=false use_gold=false'
build_openwrt mipsel_74kc 'target=ramips subtarget=rt3883' mipsel 'mips_arch_variant="r2" mips_float_abi="soft" mips_tune="74kc" use_lld=false use_gold=false'
build_openwrt mipsel_mips32 'target=rb532' mipsel 'mips_arch_variant="r1" mips_float_abi="soft" use_lld=false use_gold=false'

kill $pid
