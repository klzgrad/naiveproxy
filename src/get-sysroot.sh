eval "$EXTRA_FLAGS"

case "$(uname)" in
  MINGW*|MSYS*) host_os=win;;
  Linux) host_os=linux;;
  Darwin) host_os=mac;;
  *) echo "Unsupported host OS" >&2; exit 1;;
esac

case "$(uname -m)" in
  x86_64|x64) host_cpu=x64;;
  x86|i386|i686) host_cpu=x86;;
  arm) host_cpu=arm;;
  arm64|aarch64|armv8b|armv8l) host_cpu=arm64;;
  *) echo "Unsupported host CPU" >&2; exit 1;;
esac

# See src/build/config/BUILDCONFIG.gn
if [ ! "$target_os" ]; then
  target_os="$host_os"
fi

if [ ! "$target_cpu" ]; then
  target_cpu="$host_cpu"
fi

if which python3 >/dev/null 2>/dev/null; then
  PYTHON=python3
else
  PYTHON=python
fi

# sysroot
case "$target_os" in
  linux)
    case "$target_cpu" in
      x64) SYSROOT_ARCH=amd64;;
      x86) SYSROOT_ARCH=i386;;
      arm64) SYSROOT_ARCH=arm64;;
      arm) SYSROOT_ARCH=armhf;;
      mipsel) SYSROOT_ARCH=mipsel;;
      mips64el) SYSROOT_ARCH=mips64el;;
      riscv64) SYSROOT_ARCH=riscv64;;
    esac
    if [ "$SYSROOT_ARCH" ]; then
      WITH_SYSROOT="out/sysroot-build/bullseye/bullseye_${SYSROOT_ARCH}_staging"
    fi
    # This is the case where running ./build.sh without EXTRA_FLAGS
    # wants to avoid downloading sysroots.
    if [ ! "$EXTRA_FLAGS" -a "$target_cpu" = "$host_cpu" ]; then
      SYSROOT_ARCH=
      WITH_SYSROOT=
    fi
  ;;
  openwrt)
    eval "$OPENWRT_FLAGS"
    WITH_SYSROOT="out/sysroot-build/openwrt/$release/$arch"
  ;;
  android)
    WITH_SYSROOT=
    # https://dl.google.com/android/repository/sys-img/android/sys-img.xml
    # https://dl.google.com/android/repository/sys-img/google_apis/sys-img.xml
    case "$target_cpu" in
      x64) WITH_ANDROID_IMG=x86_64-24_r08;;
      x86) WITH_ANDROID_IMG=x86-24_r08;;
      arm64) WITH_ANDROID_IMG=arm64-v8a-24_r07;;
      arm) WITH_ANDROID_IMG=armeabi-v7a-24_r07;;
    esac
  ;;
esac
