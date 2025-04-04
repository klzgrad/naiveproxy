# -*- bazel-starlark -*-
# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Siso configuration for clang/unix."""

load("@builtin//path.star", "path")
load("@builtin//struct.star", "module")
load("./android.star", "android")
load("./ar.star", "ar")
load("./clang_code_coverage_wrapper.star", "clang_code_coverage_wrapper")
load("./config.star", "config")
load("./gn_logs.star", "gn_logs")
load("./win_sdk.star", "win_sdk")

def __clang_compile_coverage(ctx, cmd):
    clang_command = clang_code_coverage_wrapper.run(ctx, list(cmd.args))
    ctx.actions.fix(args = clang_command)

def __clang_link(ctx, cmd):
    if not config.get(ctx, "remote-link"):
        return
    inputs = []
    sysroot = ""
    target = ""
    args = cmd.args
    if args[0] == "/bin/sh":
        args = args[2].split(" ")
    for i, arg in enumerate(args):
        if i == 0 and not "python3" in arg:
            clang_base = ctx.fs.canonpath(path.dir(path.dir(arg)))
            inputs.append(clang_base + ":link")
            continue
        if i == 1:
            driver = ctx.fs.canonpath(arg)  # driver script
            if ctx.fs.exists(driver):
                inputs.append(driver + ":link")
            continue
        if arg.startswith("--sysroot="):
            sysroot = arg.removeprefix("--sysroot=")
            sysroot = ctx.fs.canonpath(sysroot)
            inputs.append(sysroot + ":link")
        elif arg == "-isysroot":
            sysroot = ctx.fs.canonpath(args[i + 1])
            inputs.append(sysroot + ":link")
        elif arg.startswith("--target="):
            target = arg.removeprefix("--target=")
        elif arg.startswith("-L"):
            lib_path = ctx.fs.canonpath(arg.removeprefix("-L"))
            inputs.append(lib_path + ":link")
        elif arg.startswith("-Wl,-exported_symbols_list,"):
            export_path = ctx.fs.canonpath(arg.removeprefix("-Wl,-exported_symbols_list,"))
            inputs.append(export_path)
        elif arg == "-sectcreate":
            # -sectcreate <arg1> <arg2> <arg3>
            inputs.append(ctx.fs.canonpath(args[i + 3]))
        elif arg.startswith("-Wcrl,"):
            crls = arg.removeprefix("-Wcrl,").split(",")
            if len(crls) == 2:
                crl = ctx.fs.canonpath(crls[1])
                if ctx.fs.exists(crl):
                    inputs.append(crl + ":link")
        elif arg == "--":
            clang_base = ctx.fs.canonpath(path.dir(path.dir(cmd.args[i + 1])))
            inputs.append(clang_base + ":link")

    for arch in android.archs:
        if target.startswith(arch):
            android_ver = target.removeprefix(arch)
            inputs.extend([
                sysroot + "/usr/lib/" + arch + "/" + android_ver + ":link",
            ])
            break

    ctx.actions.fix(inputs = cmd.inputs + inputs)

__handlers = {
    "clang_compile_coverage": __clang_compile_coverage,
    "clang_link": __clang_link,
}

def __rules(ctx):
    gn_logs_data = gn_logs.read(ctx)
    input_root_absolute_path = gn_logs_data.get("clang_need_input_root_absolute_path") == "true"
    input_root_absolute_path_for_objc = gn_logs_data.get("clang_need_input_root_absolute_path_for_objc") == "true"

    canonicalize_dir = not input_root_absolute_path
    canonicalize_dir_for_objc = not input_root_absolute_path_for_objc

    rules = []
    if win_sdk.enabled(ctx):
        rules.extend([
            {
                "name": "clang-cl/cxx",
                "action": "(.*_)?cxx",
                "command_prefix": "../../third_party/llvm-build/Release+Asserts/bin/clang-cl ",
                "inputs": [
                    "third_party/llvm-build/Release+Asserts/bin/clang-cl",
                ],
                "exclude_input_patterns": ["*.stamp"],
                "remote": True,
                "input_root_absolute_path": input_root_absolute_path,
                "canonicalize_dir": canonicalize_dir,
                "timeout": "2m",
            },
            {
                "name": "clang-cl/cc",
                "action": "(.*_)?cc",
                "command_prefix": "../../third_party/llvm-build/Release+Asserts/bin/clang-cl ",
                "inputs": [
                    "third_party/llvm-build/Release+Asserts/bin/clang-cl",
                ],
                "exclude_input_patterns": ["*.stamp"],
                "remote": True,
                "input_root_absolute_path": input_root_absolute_path,
                "canonicalize_dir": canonicalize_dir,
                "timeout": "2m",
            },
            {
                "name": "lld-link/alink",
                "action": "(.*_)?alink",
                "command_prefix": "../../third_party/llvm-build/Release+Asserts/bin/lld-link /lib",
                "handler": "lld_thin_archive",
                "remote": False,
                "accumulate": True,
            },
            {
                "name": "lld-link/solink",
                "action": "(.*_)?solink",
                "command_prefix": "../../third_party/llvm-build/Release+Asserts/bin/lld-link",
                "handler": "lld_link",
                "inputs": [
                    "third_party/llvm-build/Release+Asserts/bin/lld-link",
                    win_sdk.toolchain_dir(ctx) + ":libs",
                ],
                "exclude_input_patterns": [
                    "*.cc",
                    "*.h",
                    "*.js",
                    "*.pak",
                    "*.py",
                ],
                "remote": config.get(ctx, "remote-link"),
                "platform_ref": "large",
                "input_root_absolute_path": input_root_absolute_path,
                "canonicalize_dir": canonicalize_dir,
                "timeout": "2m",
            },
            {
                "name": "lld-link/solink_module",
                "action": "(.*_)?solink_module",
                "command_prefix": "../../third_party/llvm-build/Release+Asserts/bin/lld-link",
                "handler": "lld_link",
                "inputs": [
                    "third_party/llvm-build/Release+Asserts/bin/lld-link",
                    win_sdk.toolchain_dir(ctx) + ":libs",
                ],
                "exclude_input_patterns": [
                    "*.cc",
                    "*.h",
                    "*.js",
                    "*.pak",
                    "*.py",
                ],
                "remote": config.get(ctx, "remote-link"),
                "platform_ref": "large",
                "input_root_absolute_path": input_root_absolute_path,
                "canonicalize_dir": canonicalize_dir,
                "timeout": "2m",
            },
            {
                "name": "lld-link/link",
                "action": "(.*_)?link",
                "command_prefix": "../../third_party/llvm-build/Release+Asserts/bin/lld-link",
                "handler": "lld_link",
                "inputs": [
                    "third_party/llvm-build/Release+Asserts/bin/lld-link",
                    win_sdk.toolchain_dir(ctx) + ":libs",
                ],
                "exclude_input_patterns": [
                    "*.cc",
                    "*.h",
                    "*.js",
                    "*.pak",
                    "*.py",
                ],
                "remote": config.get(ctx, "remote-link"),
                "platform_ref": "large",
                "input_root_absolute_path": input_root_absolute_path,
                "canonicalize_dir": canonicalize_dir,
                "timeout": "2m",
            },
        ])

    rules.extend([
        {
            "name": "clang/cxx",
            "action": "(.*_)?cxx",
            "command_prefix": "../../third_party/llvm-build/Release+Asserts/bin/clang++ ",
            "inputs": [
                "third_party/llvm-build/Release+Asserts/bin/clang++",
            ],
            "exclude_input_patterns": ["*.stamp"],
            "remote": True,
            "input_root_absolute_path": input_root_absolute_path,
            "canonicalize_dir": canonicalize_dir,
            "timeout": "2m",
        },
        {
            "name": "clang/cc",
            "action": "(.*_)?cc",
            "command_prefix": "../../third_party/llvm-build/Release+Asserts/bin/clang ",
            "inputs": [
                "third_party/llvm-build/Release+Asserts/bin/clang",
            ],
            "exclude_input_patterns": ["*.stamp"],
            "remote": True,
            "input_root_absolute_path": input_root_absolute_path,
            "canonicalize_dir": canonicalize_dir,
            "timeout": "2m",
        },
        {
            "name": "clang/objcxx",
            "action": "(.*_)?objcxx",
            "command_prefix": "../../third_party/llvm-build/Release+Asserts/bin/clang++",
            "inputs": [
                "third_party/llvm-build/Release+Asserts/bin/clang++",
            ],
            "exclude_input_patterns": ["*.stamp"],
            "remote": True,
            "timeout": "2m",
            "input_root_absolute_path": input_root_absolute_path_for_objc,
            "canonicalize_dir": canonicalize_dir_for_objc,
        },
        {
            "name": "clang/objc",
            "action": "(.*_)?objc",
            "command_prefix": "../../third_party/llvm-build/Release+Asserts/bin/clang",
            "inputs": [
                "third_party/llvm-build/Release+Asserts/bin/clang",
            ],
            "exclude_input_patterns": ["*.stamp"],
            "remote": True,
            "timeout": "2m",
            "input_root_absolute_path": input_root_absolute_path_for_objc,
            "canonicalize_dir": canonicalize_dir_for_objc,
        },
        {
            "name": "clang/asm",
            "action": "(.*_)?asm",
            "command_prefix": "../../third_party/llvm-build/Release+Asserts/bin/clang",
            "inputs": [
                "third_party/llvm-build/Release+Asserts/bin/clang",
            ],
            "remote": config.get(ctx, "cog"),
            "input_root_absolute_path": input_root_absolute_path,
            "canonicalize_dir": canonicalize_dir,
            "timeout": "2m",
        },
        {
            "name": "clang-coverage/cxx",
            "action": "(.*_)?cxx",
            "command_prefix": "\"python3\" ../../build/toolchain/clang_code_coverage_wrapper.py",
            "inputs": [
                "third_party/llvm-build/Release+Asserts/bin/clang++",
            ],
            "exclude_input_patterns": ["*.stamp"],
            "handler": "clang_compile_coverage",
            "remote": True,
            "input_root_absolute_path": input_root_absolute_path,
            "canonicalize_dir": canonicalize_dir,
            "timeout": "2m",
        },
        {
            "name": "clang-coverage/cc",
            "action": "(.*_)?cc",
            "command_prefix": "\"python3\" ../../build/toolchain/clang_code_coverage_wrapper.py",
            "inputs": [
                "third_party/llvm-build/Release+Asserts/bin/clang",
            ],
            "exclude_input_patterns": ["*.stamp"],
            "handler": "clang_compile_coverage",
            "remote": True,
            "input_root_absolute_path": input_root_absolute_path,
            "canonicalize_dir": canonicalize_dir,
            "timeout": "2m",
        },
        {
            "name": "clang-coverage/objcxx",
            "action": "(.*_)?objcxx",
            "command_prefix": "\"python3\" ../../build/toolchain/clang_code_coverage_wrapper.py",
            "inputs": [
                "third_party/llvm-build/Release+Asserts/bin/clang++",
            ],
            "exclude_input_patterns": ["*.stamp"],
            "handler": "clang_compile_coverage",
            "remote": True,
            "timeout": "2m",
            "input_root_absolute_path": input_root_absolute_path_for_objc,
            "canonicalize_dir": canonicalize_dir_for_objc,
        },
        {
            "name": "clang-coverage/objc",
            "action": "(.*_)?objc",
            "command_prefix": "\"python3\" ../../build/toolchain/clang_code_coverage_wrapper.py",
            "inputs": [
                "third_party/llvm-build/Release+Asserts/bin/clang",
            ],
            "exclude_input_patterns": ["*.stamp"],
            "handler": "clang_compile_coverage",
            "remote": True,
            "timeout": "2m",
            "input_root_absolute_path": input_root_absolute_path_for_objc,
            "canonicalize_dir": canonicalize_dir_for_objc,
        },
        {
            "name": "clang/alink/llvm-ar",
            "action": "(.*_)?alink",
            "inputs": [
                # TODO: crbug.com/316267242 - Add inputs to GN config.
                "third_party/llvm-build/Release+Asserts/bin/llvm-ar",
            ],
            "exclude_input_patterns": [
                "*.cc",
                "*.h",
                "*.js",
                "*.pak",
                "*.py",
                "*.stamp",
            ],
            "handler": "lld_thin_archive",
            "remote": config.get(ctx, "remote-link"),
            "canonicalize_dir": True,
            "timeout": "2m",
            "platform_ref": "large",
            "accumulate": True,
        },
        {
            "name": "clang/solink",
            "action": "(.*_)?solink",
            "handler": "clang_link",
            "exclude_input_patterns": [
                "*.cc",
                "*.h",
                "*.js",
                "*.pak",
                "*.py",
                "*.stamp",
            ],
            "remote": config.get(ctx, "remote-link"),
            "restat_content": True,
            "canonicalize_dir": True,
            "platform_ref": "large",
            "timeout": "4m",
        },
        {
            "name": "clang/solink_module",
            "action": "(.*)?solink_module",
            "handler": "clang_link",
            "exclude_input_patterns": [
                "*.cc",
                "*.h",
                "*.js",
                "*.pak",
                "*.py",
                "*.stamp",
            ],
            "remote": config.get(ctx, "remote-link"),
            "canonicalize_dir": True,
            "platform_ref": "large",
            "timeout": "4m",
        },
        {
            "name": "clang/link",
            "action": "(.*_)?link",
            "handler": "clang_link",
            "exclude_input_patterns": [
                "*.cc",
                "*.h",
                "*.js",
                "*.pak",
                "*.py",
                "*.stamp",
            ],
            "remote": config.get(ctx, "remote-link"),
            "canonicalize_dir": True,
            "platform_ref": "large",
            "timeout": "10m",
        },
    ])
    return rules

clang_unix = module(
    "clang_unix",
    handlers = __handlers,
    rules = __rules,
)
