get_sysroot() {
  eval "$EXTRA_FLAGS"
  if [ "$OPENWRT_ARCH" -a "$OPENWRT_RELEASE" ]; then
     echo "out/sysroot-build/openwrt/$OPENWRT_RELEASE/$OPENWRT_ARCH"
  elif [ ! "$target_sysroot" ]; then
    local sysroot_type
    case "$target_cpu" in
      x64) sysroot_type=amd64;;
      x86) sysroot_type=i386;;
      arm64) sysroot_type=arm64;;
      arm) sysroot_type=arm;;
      mipsel) sysroot_type=mips;;
    esac
    if [ "$sysroot_type" ]; then
      echo "out/sysroot-build/sid/sid_${sysroot_type}_staging"
    fi
  fi
}
