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
    WITH_PGO=linux
    WITH_GN=linux
    case "$target_cpu" in
      x64) WITH_QEMU=x86_64;;
      x86) WITH_QEMU=i386;;
      arm64) WITH_QEMU=aarch64;;
      arm) WITH_QEMU=arm;;
      mipsel) WITH_QEMU=mipsel;;
      mips64el) WITH_QEMU=mips64el;;
    esac
    if [ "$OPENWRT_FLAGS" ]; then
      eval "$OPENWRT_FLAGS"
      WITH_SYSROOT="out/sysroot-build/openwrt/$release/$arch"
    elif [ "$target_os" = android ]; then
      WITH_PGO=
      USE_AFDO=y
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
        x64) sysroot_path=amd64 BUILD_SYSROOT=BuildSysrootAmd64;;
        x86) sysroot_path=i386 BUILD_SYSROOT=BuildSysrootI386;;
        arm64) sysroot_path=arm64 BUILD_SYSROOT=BuildSysrootARM64;;
        arm) sysroot_path=arm BUILD_SYSROOT=BuildSysrootARM;;
        mipsel) sysroot_path=mips BUILD_SYSROOT=BuildSysrootMips;;
        mips64el) sysroot_path=mips64el BUILD_SYSROOT=BuildSysrootMips64el;;
      esac
      if [ "$sysroot_path" ]; then
        WITH_SYSROOT="out/sysroot-build/bullseye/bullseye_${sysroot_path}_staging"
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
    WITH_GN=windows
    case "$target_cpu" in
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
    WITH_CLANG=Mac
    WITH_GN=mac
    case "$target_cpu" in
      arm64) WITH_PGO=mac-arm;;
      *) WITH_PGO=mac;;
    esac
  ;;
esac
if [ "$WITH_PGO" ]; then
  PGO_PATH=$(cat chrome/build/$WITH_PGO.pgo.txt)
fi
