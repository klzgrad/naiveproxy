#!/bin/sh
set -ex

. ./get-sysroot.sh

if [ "$BUILD_SYSROOT" -a ! -d ./"$WITH_SYSROOT/lib" ]; then
  ./build/linux/sysroot_scripts/sysroot-creator-naive.sh "$BUILD_SYSROOT"
fi

if [ "$OPENWRT_FLAGS" ]; then
  ./get-openwrt.sh
fi

if [ ! -d third_party/llvm-build/Release+Asserts/bin ]; then
  mkdir -p third_party/llvm-build/Release+Asserts
  clang_path="clang-$CLANG_REVISION.tgz"
  clang_url="https://commondatastorage.googleapis.com/chromium-browser-clang/$WITH_CLANG/$clang_path"
  curl "$clang_url" | tar xzf - -C third_party/llvm-build/Release+Asserts
fi

if [ "$USE_AFDO" -a ! -f chrome/android/profiles/afdo.prof ]; then
  afdo_path=$(cat chrome/android/profiles/newest.txt)
  afdo_url="https://storage.googleapis.com/chromeos-prebuilt/afdo-job/llvm/$afdo_path"
  curl "$afdo_url" | bzip2 -cd >chrome/android/profiles/afdo.prof
fi

if [ "$WITH_PGO" -a ! -f chrome/build/pgo_profiles/"$PGO_PATH" ]; then
  mkdir -p chrome/build/pgo_profiles
  cd chrome/build/pgo_profiles
  curl --limit-rate 10M -LO "https://storage.googleapis.com/chromium-optimization-profiles/pgo_profiles/$PGO_PATH"
  cd ../../..
fi

if [ "$USE_SCCACHE" -a ! -f ~/.cargo/bin/sccache.exe ]; then
  sccache_url="https://github.com/mozilla/sccache/releases/download/0.2.12/sccache-0.2.12-x86_64-pc-windows-msvc.tar.gz"
  mkdir -p ~/.cargo/bin
  curl -L "$sccache_url" | tar xzf - --strip=1 -C ~/.cargo/bin
fi

if [ ! -f gn/out/gn ]; then
  gn_version=$(grep "'gn_version':" DEPS | cut -d"'" -f4)
  mkdir -p gn/out
  curl -L "https://chrome-infra-packages.appspot.com/dl/gn/gn/$WITH_GN-amd64/+/$gn_version" -o gn.zip
  unzip gn.zip -d gn/out
  rm gn.zip
fi

if [ "$USE_ANDROID_NDK" -a ! -d third_party/android_ndk ]; then
  android_ndk_version=$(grep android_ndk.git DEPS | cut -d"'" -f10)
  git clone --depth=1 --filter=blob:none --no-checkout https://chromium.googlesource.com/android_ndk.git third_party/android_ndk
  cd third_party/android_ndk
  git sparse-checkout init --cone
  git sparse-checkout set sources/android/cpufeatures toolchains/llvm/prebuilt
  git -c advice.detachedHead=false checkout "$android_ndk_version"
  rm -rf .git
  find toolchains -type f -regextype egrep \! -regex \
    '.*(lib(atomic|gcc|gcc_real|compiler_rt-extras|android_support|unwind).a|crt.*o|lib(android|c|dl|log|m).so|usr/local.*|usr/include.*)' -delete
  cd ../..
fi
