#!/bin/sh
set -ex

ARCH=$(uname)
case "$ARCH" in
  MINGW*) ARCH=Windows;;
  MSYS*) ARCH=Windows;;
esac

build_sysroot() {
  . ./get-sysroot.sh
  sysroot=$(get_sysroot)
  if [ -d $sysroot/lib ]; then
    return
  fi
  ./build/linux/sysroot_scripts/sysroot-creator-sid-naive.sh "BuildSysroot$1"
}

if [ "$ARCH" = Linux ]; then
  if [ "$OPENWRT_FLAGS" ]; then
    ./get-openwrt.sh
  else
    eval "$EXTRA_FLAGS"
    case "$target_cpu" in
      x64) build_sysroot Amd64;;
      x86) build_sysroot I386;;
      arm64) build_sysroot ARM64;;
      arm) build_sysroot ARM;;
    esac
  fi
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

# Profiles (Windows, Mac)
case "$ARCH" in
  Linux) PGO_NAME=linux;;
  Windows)
    case "$(uname -m)" in
      x86_64) PGO_NAME=win64;;
      *) PGO_NAME=win32;;
    esac;;
  Darwin) PGO_NAME=mac;;
esac
if [ "$PGO_NAME" ]; then
  mkdir -p chrome/build/pgo_profiles
  profile=$(cat chrome/build/$PGO_NAME.pgo.txt)
  cd chrome/build/pgo_profiles
  if [ ! -f "$profile" ]; then
    curl --limit-rate 10M -LO "https://storage.googleapis.com/chromium-optimization-profiles/pgo_profiles/$profile"
  fi
  cd ../../..
fi

# dsymutil (Mac)
if [ "$ARCH" = Darwin -a ! -f tools/clang/dsymutil/bin/dsymutil ]; then
  mkdir -p tools/clang/dsymutil
  DSYMUTIL_PATH="dsymutil-$CLANG_REVISION.tgz"
  dsymutil_url="https://commondatastorage.googleapis.com/chromium-browser-clang-staging/Mac/$DSYMUTIL_PATH"
  curl "$dsymutil_url" | tar xzf - -C tools/clang/dsymutil
fi

# sccache (Windows)
if [ "$ARCH" = Windows -a ! -f ~/.cargo/bin/sccache.exe ]; then
  sccache_url="https://github.com/mozilla/sccache/releases/download/v0.2.15/sccache-v0.2.15-x86_64-pc-windows-msvc.tar.gz"
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
