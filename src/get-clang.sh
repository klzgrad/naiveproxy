#!/bin/sh
set -ex

ARCH=$(uname)
case "$ARCH" in
  MINGW*) ARCH=Windows;;
  MSYS*) ARCH=Windows;;
esac

# Clang
CLANG_REVISION=$(grep -m1 CLANG_REVISION tools/clang/scripts/update.py | cut -d"'" -f2)
CLANG_SUB_REVISION=$(grep -m1 CLANG_SUB_REVISION tools/clang/scripts/update.py | cut -d= -f2)
CLANG_PATH="clang-$CLANG_REVISION-$CLANG_SUB_REVISION.tgz"
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

# sccache (Windows)
if [ "$ARCH" = Windows ]; then
  sccache_url="https://github.com/mozilla/sccache/releases/download/0.2.8/sccache-0.2.8-x86_64-pc-windows-msvc.tar.gz"
  mkdir -p ~/.cargo/bin
  curl -L "$sccache_url" | tar xzf - --strip=1 -C ~/.cargo/bin
fi

# gn
if [ ! -f gn/out/gn ]; then
  mkdir -p gn/out
  cd gn/out
  case "$ARCH" in
    Linux) curl -L https://chrome-infra-packages.appspot.com/dl/gn/gn/linux-amd64/+/latest -o gn.zip;;
    Darwin) curl -L https://chrome-infra-packages.appspot.com/dl/gn/gn/mac-amd64/+/latest -o gn.zip;;
    Windows) curl -L https://chrome-infra-packages.appspot.com/dl/gn/gn/windows-amd64/+/latest -o gn.zip;;
  esac
  unzip gn.zip
fi
