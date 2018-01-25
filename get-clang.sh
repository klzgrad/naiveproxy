#!/bin/sh
set -ex

CLANG_REVISION=$(grep -m1 CLANG_REVISION tools/clang/scripts/update.py | cut -d"'" -f2)
CLANG_SUB_REVISION=$(grep -m1 CLANG_SUB_REVISION tools/clang/scripts/update.py | cut -d= -f2)
url="https://commondatastorage.googleapis.com/chromium-browser-clang/Linux_x64/clang-$CLANG_REVISION-$CLANG_SUB_REVISION.tgz"
mkdir -p third_party/llvm-build/Release+Asserts
cd third_party/llvm-build/Release+Asserts
curl "$url" -o- | tar xzf -
