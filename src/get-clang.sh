#!/bin/sh
set -ex

. ./get-sysroot.sh

if [ "$SYSROOT_ARCH" -a ! -d ./"$WITH_SYSROOT/lib" ]; then
  ./build/linux/sysroot_scripts/sysroot-creator.sh build "$SYSROOT_ARCH"
fi

if [ "$OPENWRT_FLAGS" ]; then
  ./get-openwrt.sh
fi

# Clang
# See src/tools/clang/scripts/update.py
case "$host_os" in
  linux) WITH_CLANG=Linux_x64;;
  win) WITH_CLANG=Win;;
  mac) WITH_CLANG=Mac;;
esac
if [ "$host_os" = mac -a "$host_cpu" = arm64 ]; then
  WITH_CLANG=Mac_arm64
fi
mkdir -p third_party/llvm-build/Release+Asserts
cd tools/clang/scripts
CLANG_REVISION=$($PYTHON -c 'import update; print(update.PACKAGE_VERSION)')
cd -
echo $CLANG_REVISION >third_party/llvm-build/Release+Asserts/cr_build_revision
if [ ! -d third_party/llvm-build/Release+Asserts/bin ]; then
  mkdir -p third_party/llvm-build/Release+Asserts
  clang_path="clang-$CLANG_REVISION.tgz"
  clang_url="https://commondatastorage.googleapis.com/chromium-browser-clang/$WITH_CLANG/$clang_path"
  curl "$clang_url" | tar xzf - -C third_party/llvm-build/Release+Asserts
fi

# sccache
if [ "$host_os" = win -a ! -f ~/.cargo/bin/sccache.exe ]; then
  sccache_url="https://github.com/mozilla/sccache/releases/download/0.2.12/sccache-0.2.12-x86_64-pc-windows-msvc.tar.gz"
  mkdir -p ~/.cargo/bin
  curl -L "$sccache_url" | tar xzf - --strip=1 -C ~/.cargo/bin
fi

# GN
# See src/DEPS
case "$host_os" in
  linux) WITH_GN=linux-amd64;;
  win) WITH_GN=windows-amd64;;
  mac) WITH_GN=mac-amd64;;
esac
if [ "$host_os" = mac -a "$host_cpu" = arm64 ]; then
  WITH_GN=mac-arm64
fi
if [ ! -f gn/out/gn ]; then
  gn_version=$(grep "'gn_version':" DEPS | cut -d"'" -f4)
  mkdir -p gn/out
  curl -L "https://chrome-infra-packages.appspot.com/dl/gn/gn/$WITH_GN/+/$gn_version" -o gn.zip
  unzip gn.zip -d gn/out
  rm gn.zip
fi

# See src/build/config/compiler/pgo/BUILD.gn
case "$target_os" in
  win)
    case "$target_cpu" in
      arm64) WITH_PGO=win-arm64;;
      x64) WITH_PGO=win64;;
      *) WITH_PGO=win32;;
    esac
  ;;
  mac)
    case "$target_cpu" in
      arm64) WITH_PGO=mac-arm;;
      *) WITH_PGO=mac;;
    esac
  ;;
  linux|openwrt)
    WITH_PGO=linux
  ;;
  android)
    case "$target_cpu" in
      arm64) WITH_PGO=android-arm64;;
      *) WITH_PGO=android-arm32;;
    esac
  ;;
esac
if [ "$WITH_PGO" ]; then
  PGO_PATH=$(cat chrome/build/$WITH_PGO.pgo.txt)
fi
if [ "$WITH_PGO" -a ! -f chrome/build/pgo_profiles/"$PGO_PATH" ]; then
  mkdir -p chrome/build/pgo_profiles
  cd chrome/build/pgo_profiles
  curl --limit-rate 10M -LO "https://storage.googleapis.com/chromium-optimization-profiles/pgo_profiles/$PGO_PATH"
  cd ../../..
fi

if [ "$target_os" = android -a ! -d third_party/android_toolchain/ndk ]; then
  # https://dl.google.com/android/repository/android-ndk-r25c-linux.zip
  android_ndk_version=$(grep 'default_android_ndk_version = ' build/config/android/config.gni | cut -d'"' -f2)
  curl -LO https://dl.google.com/android/repository/android-ndk-$android_ndk_version-linux.zip
  unzip android-ndk-$android_ndk_version-linux.zip
  mkdir -p third_party/android_toolchain/ndk
  cd android-ndk-$android_ndk_version
  cp -r --parents sources/android/cpufeatures ../third_party/android_toolchain/ndk
  cp -r --parents toolchains/llvm/prebuilt ../third_party/android_toolchain/ndk
  cd ..
  cd third_party/android_toolchain/ndk
  find toolchains -type f -regextype egrep \! -regex \
    '.*(lib(atomic|gcc|gcc_real|compiler_rt-extras|android_support|unwind).a|crt.*o|lib(android|c|dl|log|m).so|usr/local.*|usr/include.*)' -delete
  cd -
  rm -rf android-ndk-$android_ndk_version android-ndk-$android_ndk_version-linux.zip
fi
