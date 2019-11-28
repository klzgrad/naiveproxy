#!/bin/sh
set -ex

ARCH=$(uname)
case "$ARCH" in
  MINGW*) ARCH=Windows;;
  MSYS*) ARCH=Windows;;
esac

eval "$EXTRA_FLAGS"

build_sysroot() {
  local lower="$(echo "$1" | tr '[:upper:]' '[:lower:]')"
  ./build/linux/sysroot_scripts/sysroot-creator-sid-naive.sh "BuildSysroot$1"
  rm -rf "./build/linux/debian_sid_$lower-sysroot"
  mkdir "./build/linux/debian_sid_$lower-sysroot"
  tar xf "./out/sysroot-build/sid/debian_sid_${lower}_sysroot.tar.xz" -C "./build/linux/debian_sid_$lower-sysroot"
}

if [ "$ARCH" = Linux ]; then
  build_sysroot Amd64
  case "$target_cpu" in
    arm64)
      build_sysroot ARM64
    ;;
    arm)
      build_sysroot I386
      build_sysroot ARM
    ;;
    x86)
      build_sysroot I386
    ;;
    mips64el)
      build_sysroot Mips64el
    ;;
    mipsel)
      build_sysroot I386
      build_sysroot Mips
  esac
fi

# Clang
python2=$(which python2 2>/dev/null || which python 2>/dev/null)
CLANG_REVISION=$($python2 tools/clang/scripts/update.py --print-revision)
CLANG_PATH="clang-$CLANG_REVISION.tgz"
case "$ARCH" in
  Linux) clang_url="https://commondatastorage.googleapis.com/chromium-browser-clang/Linux_x64/$CLANG_PATH";;
  Darwin) clang_url="https://commondatastorage.googleapis.com/chromium-browser-clang/Mac/$CLANG_PATH";;
  Windows) clang_url="https://commondatastorage.googleapis.com/chromium-browser-clang/Win/$CLANG_PATH";;
  *) exit 1;;
esac
if [ ! -d third_party/llvm-build/Release+Asserts/bin ]; then
  mkdir -p third_party/llvm-build/Release+Asserts
  curl "$clang_url" | tar xzf - -C third_party/llvm-build/Release+Asserts
fi

# AFDO profile (Linux)
if [ "$ARCH" = Linux -a ! -f chrome/android/profiles/afdo.prof ]; then
  AFDO_PATH=$(cat chrome/android/profiles/newest.txt)
  afdo_url="https://storage.googleapis.com/chromeos-prebuilt/afdo-job/llvm/$AFDO_PATH"
  curl "$afdo_url" | bzip2 -cd >chrome/android/profiles/afdo.prof
fi

# dsymutil (Mac)
if [ "$ARCH" = Darwin ]; then
  mkdir -p tools/clang/dsymutil
  DSYMUTIL_PATH="dsymutil-$CLANG_REVISION.tgz"
  dsymutil_url="http://commondatastorage.googleapis.com/chromium-browser-clang-staging/Mac/$DSYMUTIL_PATH"
  curl "$dsymutil_url" | tar xzf - -C tools/clang/dsymutil
fi

# sccache (Windows)
if [ "$ARCH" = Windows ]; then
  sccache_url="https://github.com/mozilla/sccache/releases/download/0.2.8/sccache-0.2.8-x86_64-pc-windows-msvc.tar.gz"
  mkdir -p ~/.cargo/bin
  curl -L "$sccache_url" | tar xzf - --strip=1 -C ~/.cargo/bin
fi

# gn
if [ ! -f gn/out/gn ]; then
  GN_VERSION=$(grep "'gn_version':" buildtools/DEPS | cut -d"'" -f4)
  mkdir -p gn/out
  cd gn/out
  case "$ARCH" in
    Linux) curl -L "https://chrome-infra-packages.appspot.com/dl/gn/gn/linux-amd64/+/$GN_VERSION" -o gn.zip;;
    Darwin) curl -L "https://chrome-infra-packages.appspot.com/dl/gn/gn/mac-amd64/+/$GN_VERSION" -o gn.zip;;
    Windows) curl -L "https://chrome-infra-packages.appspot.com/dl/gn/gn/windows-amd64/+/$GN_VERSION" -o gn.zip;;
  esac
  unzip gn.zip
fi
