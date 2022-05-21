#!/bin/sh

set -ex

WINDOWS_LLVM_VERSION=13.0.1
MAC_SDK_VERSION=12.1

# CGO does not support relative path very well. TODO: better way to handle this using SRCDIR?
# for i in go_env.sh link_shared.go link_static.go; do
#   sed "s#\./sysroot#$PWD/sysroot#g" $i >$i.1
#   mv $i.1 $i
# done

# Imports environment variables: GOOS, GOARCH, GOMIPS, CGO_CFLAGS, CGO_LDFLAGS
# Imports bash variables: ARCH, target_cpu, CLANG_REVISION, WITH_CLANG, WITH_QEMU, WITH_ANDROID_IMG, buildmode_flag
. ./go_env.sh

if [ "$ARCH" = 'Windows' ]; then
  alias ln='MSYS=winsymlinks:nativestrict ln'
  exe_extension=.exe
fi

# Gets LLVM
# Recommends copying cached content to here to avoid repeatedly downloading from googleapis.com.
if [ ! -d ./llvm/bin ]; then
  if [ -d ../../../third_party/llvm-build/Release+Asserts/bin ]; then
    # Reuses existing toolchain if running in naiveproxy build tree
    ln -sfn $PWD/../../../third_party/llvm-build/Release+Asserts ./llvm
  else
    mkdir -p ./llvm
    clang_path="clang-$CLANG_REVISION.tgz"
    clang_url="https://commondatastorage.googleapis.com/chromium-browser-clang/$WITH_CLANG/$clang_path"
    curl "$clang_url" | tar xzf - -C ./llvm
  fi
fi

if [ "$ARCH" = 'Windows' ]; then
  ln -sfn "C:/Program Files/LLVM/lib/clang/$WINDOWS_LLVM_VERSION" ./llvm/lib/clang/
  cat >lld-link.cc <<EOF
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
  std::string cmd = "lld-link-old";
  for (int i = 1; i < argc; ++i) {
    fprintf(stderr, "argv[%d]: %s\n", i, argv[i]);
    if (strcmp(argv[i], "--tsaware") == 0) continue;
    if (strcmp(argv[i], "--nxcompat") == 0) continue;
    if (strstr(argv[i], "--major-os-version=") == argv[i]) continue;
    if (strstr(argv[i], "--minor-os-version=") == argv[i]) continue;
    if (strstr(argv[i], "--major-subsystem-version=") == argv[i]) continue;
    if (strstr(argv[i], "--minor-subsystem-version=") == argv[i]) continue;
    if (strcmp(argv[i], "--dynamicbase") == 0) continue;
    if (strcmp(argv[i], "--high-entropy-va") == 0) continue;
    if (strcmp(argv[i], "-T") == 0) {
      ++i;
      continue;
    }
    if (strcmp(argv[i], "--start-group") == 0) continue;
    if (strcmp(argv[i], "mingwex.lib") == 0) continue;
    if (strcmp(argv[i], "mingw32.lib") == 0) continue;
    if (strcmp(argv[i], "--end-group") == 0) continue;
    if (strchr(argv[i], ' ') != nullptr) {
      cmd.append(" \"").append(argv[i]).append("\"");
    } else {
      cmd.append(" ").append(argv[i]);
    }
  }
  fprintf(stderr, "cmd: %s\n", cmd.c_str());
  return system(cmd.c_str());
}
EOF
  if [ ! -f ./llvm/bin/lld-link-old.exe ]; then
    cp 'C:\Program Files\LLVM\bin\clang.exe' ./llvm/bin/
    mv ./llvm/bin/lld-link.exe ./llvm/bin/lld-link-old.exe
    clang lld-link.cc -o ./llvm/bin/lld-link.exe
  fi
fi

# Finds Mac SDK path for sysroot, following build/mac/find_sdk.py.
if [ "$ARCH" = 'Darwin' ]; then
  mac_sdk_path="$(xcode-select -print-path)"/Platforms/MacOSX.platform/Developer/SDKs/MacOSX$MAC_SDK_VERSION.sdk
  if [ ! -e "$mac_sdk_path" ]; then
    echo 'MacOS SDK not found'
    exit 1
  fi
  ln -sfn "$mac_sdk_path" ./sysroot
fi

export PATH="$PWD/llvm/bin:$PATH"
export CC=clang
export CGO_ENABLED=1
export CGO_LDFLAGS_ALLOW=.*

run_cronet_example() {
  local rootfs=./sysroot
  if [ "$WITH_ANDROID_IMG" ]; then
    # Only supports testing in tree because the rootfs is very large and the script to download it is not trivial.
    rootfs="$PWD/../../../out/sysroot-build/android/$WITH_ANDROID_IMG"
    if [ ! -d "$rootfs" ]; then
      echo 'Skips testing cronet_example due to missing Android rootfs'
      return
    fi
  fi
  if [ "$WITH_QEMU" ]; then
    if [ "$target_cpu" = "x64" -o "$target_cpu" = "x86" ]; then
      cp libcronet.so cronet_example "$rootfs"
      bwrap --bind "$rootfs" / --proc /proc --dev /dev --setenv LD_LIBRARY_PATH / /cronet_example "$@"
      rm -f "$rootfs"/libcronet.so "$rootfs"/cronet_example
    else
      # Older qemu-user cannot run CGO binaries in MIPS, see https://github.com/golang/go/issues/33746.
      # Newer qemu-user-static can be separately installed.
      qemu-$WITH_QEMU-static -L "$rootfs" ./cronet_example "$@"
    fi
  elif [ "$target_cpu" = "arm64" -a "$ARCH" = "Darwin" ]; then
    echo 'Skips testing cronet_example'
  elif [ "$target_cpu" = "arm64" -a "$ARCH" = "Windows" ]; then
    echo 'Skips testing cronet_example'
  else
    ./cronet_example "$@"
  fi
}

go build $buildmode_flag cronet_example.go link_shared.go
run_cronet_example http://example.com
if [ "$ARCH" = "Linux" ]; then
  ./llvm/bin/llvm-strip cronet_example
fi
ls -l cronet_example${exe_extension}
rm -f cronet_example${exe_extension}

go build $buildmode_flag cronet_example.go link_static.go
run_cronet_example http://example.com
if [ "$ARCH" = "Linux" ]; then
  ./llvm/bin/llvm-strip cronet_example
fi
ls -l cronet_example${exe_extension}
rm -f cronet_example${exe_extension}

# If it is a symlink, it points to pre-installed toolchain. Avoids packaging it.
if [ -h ./sysroot ]; then
  rm -f ./sysroot
fi

# If it is a symlink, it points to pre-installed toolchain. Avoids packaging it.
if [ -h ./llvm ]; then
  rm -f ./llvm
fi

# Generated by ThinLTO linking.
rm -rf ./thinlto-cache
