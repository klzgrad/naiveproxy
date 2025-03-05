# -*- bazel-starlark -*-
# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Siso configuration for clang."""

load("@builtin//struct.star", "module")
load("./mac_sdk.star", "mac_sdk")
load("./win_sdk.star", "win_sdk")

def __filegroups(ctx):
    fg = {
        "third_party/libc++/src/include:headers": {
            "type": "glob",
            "includes": ["*"],
            # can't use "*.h", because c++ headers have no extension.
        },
        "third_party/libc++abi/src/include:headers": {
            "type": "glob",
            "includes": ["*.h"],
        },
        # vendor provided headers for libc++.
        "buildtools/third_party/libc++:headers": {
            "type": "glob",
            "includes": [
                "__*",
            ],
        },

        # toolchain root
        # :headers for compiling
        "third_party/llvm-build/Release+Asserts:headers": {
            "type": "glob",
            "includes": [
                "*.h",
                "bin/clang",
                "bin/clang++",
                "bin/clang-*",  # clang-cl, clang-<ver>
                "*_ignorelist.txt",
                # https://crbug.com/335997052
                "clang_rt.profile*.lib",
            ],
        },
        "third_party/cronet_android_mainline_clang/linux-amd64:headers": {
            "type": "glob",
            "includes": [
                "*.h",
                "bin/clang*",
            ],
        },
        "third_party/cronet_android_mainline_clang/linux-amd64:link": {
            "type": "glob",
            "includes": [
                "bin/clang*",
                "bin/ld.lld",
                "bin/lld",
                "bin/llvm-nm",
                "bin/llvm-objcopy",
                "bin/llvm-readelf",
                "bin/llvm-readobj",
                "bin/llvm-strip",
                "*.so",
                "*.so.*",
                "*.a",
            ],
        },
    }
    if win_sdk.enabled(ctx):
        fg.update(win_sdk.filegroups(ctx))
    if mac_sdk.enabled(ctx):
        fg.update(mac_sdk.filegroups(ctx))
    return fg

__input_deps = {
    # need this because we use
    # third_party/libc++/src/include:headers,
    # but scandeps doesn't scan `__config` file, which uses
    # `#include <__config_site>`
    # also need `__assertion_handler`. b/321171148
    "third_party/libc++/src/include": [
        "buildtools/third_party/libc++:headers",
    ],
    "third_party/llvm-build/Release+Asserts/bin/clang": [
        "build/config/unsafe_buffers_paths.txt",
    ],
    "third_party/llvm-build/Release+Asserts/bin/clang++": [
        "build/config/unsafe_buffers_paths.txt",
    ],
    "third_party/llvm-build/Release+Asserts/bin/clang-cl": [
        "build/config/unsafe_buffers_paths.txt",
    ],
    "third_party/llvm-build/Release+Asserts/bin/clang-cl.exe": [
        "build/config/unsafe_buffers_paths.txt",
    ],
    "build/toolchain/gcc_solink_wrapper.py": [
        "build/toolchain/whole_archive.py",
        "build/toolchain/wrapper_utils.py",
    ],
    "build/toolchain/gcc_solink_wrapper.py:link": [
        "build/toolchain/gcc_solink_wrapper.py",
        "build/toolchain/whole_archive.py",
        "build/toolchain/wrapper_utils.py",
    ],
    "build/toolchain/gcc_link_wrapper.py": [
        "build/toolchain/whole_archive.py",
        "build/toolchain/wrapper_utils.py",
    ],
    "build/toolchain/gcc_link_wrapper.py:link": [
        "build/toolchain/gcc_link_wrapper.py",
        "build/toolchain/whole_archive.py",
        "build/toolchain/wrapper_utils.py",
    ],
    "build/toolchain/apple/linker_driver.py:link": [
        "build/toolchain/apple/linker_driver.py",
        "build/toolchain/whole_archive.py",
    ],
    "build/toolchain/apple/solink_driver.py:link": [
        "build/toolchain/apple/linker_driver.py",
        "build/toolchain/apple/solink_driver.py",
        "build/toolchain/whole_archive.py",
    ],
}

clang_all = module(
    "clang_all",
    filegroups = __filegroups,
    input_deps = __input_deps,
)
