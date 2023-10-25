ARCH=$(uname)

PYTHON=$(which python3 2>/dev/null || which python 2>/dev/null)
mkdir -p third_party/llvm-build/Release+Asserts
cd tools/clang/scripts
CLANG_REVISION=$($PYTHON -c 'import update; print(update.PACKAGE_VERSION)')
cd -
echo $CLANG_REVISION >third_party/llvm-build/Release+Asserts/cr_build_revision

eval "$EXTRA_FLAGS"
case "$ARCH" in
  Linux)
    if which ccache >/dev/null 2>&1; then
      export CCACHE_SLOPPINESS=time_macros
      export CCACHE_BASEDIR="$PWD"
      export CCACHE_CPP2=yes
      CCACHE=ccache
    fi
    WITH_CLANG=Linux_x64
    # See build/config/compiler/pgo/BUILD.gn
    WITH_PGO=linux
    WITH_GN=linux-amd64
    if [ "$OPENWRT_FLAGS" ]; then
      eval "$OPENWRT_FLAGS"
      WITH_SYSROOT="out/sysroot-build/openwrt/$release/$arch"
    elif [ "$target_os" = android ]; then
      case "$target_cpu" in
        arm64) WITH_PGO=android-arm64;;
        *) WITH_PGO=android-arm32;;
      esac
      # Continue to use mac-arm profile while investigating Android results.
      # TODO(crbug.com/4828524, crbug.com/1308749): Remove the following.
      WITH_PGO=mac-arm
      USE_AFDO=
      USE_ANDROID_NDK=y
      WITH_SYSROOT=
      case "$target_cpu" in
        x64) WITH_ANDROID_IMG=x86_64-24_r08;;
        x86) WITH_ANDROID_IMG=x86-24_r08;;
        arm64) WITH_ANDROID_IMG=arm64-v8a-24_r07;;
        arm) WITH_ANDROID_IMG=armeabi-v7a-24_r07;;
      esac
    else
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
    fi
  ;;
  MINGW*|MSYS*)
    ARCH=Windows
    if [ -f "$HOME"/.cargo/bin/sccache* ]; then
      export PATH="$PATH:$HOME/.cargo/bin"
      CCACHE=sccache
    fi
    WITH_CLANG=Win
    USE_SCCACHE=y
    WITH_GN=windows-amd64
    case "$target_cpu" in
      arm64) WITH_PGO=win-arm64;;
      x64) WITH_PGO=win64;;
      *) WITH_PGO=win32;;
    esac
  ;;
  Darwin)
    if which ccache >/dev/null 2>&1; then
      export CCACHE_SLOPPINESS=time_macros
      export CCACHE_BASEDIR="$PWD"
      export CCACHE_CPP2=yes
      CCACHE=ccache
    fi
    MACHINE=$(uname -m)
    if [ "$MACHINE" = "arm64" ]; then
      WITH_CLANG=Mac_arm64
      WITH_GN=mac-arm64
      WITH_PGO_MACHINE=mac-arm
    else
      WITH_CLANG=Mac
      WITH_GN=mac-amd64
      WITH_PGO_MACHINE=mac
    fi
    case "$target_cpu" in
      arm64) WITH_PGO=mac-arm;;
      x64) WITH_PGO=mac;;
      *) WITH_PGO="$WITH_PGO_MACHINE";;
    esac
  ;;
esac
if [ "$WITH_PGO" ]; then
  PGO_PATH=$(cat chrome/build/$WITH_PGO.pgo.txt)
fi
