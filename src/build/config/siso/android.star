# -*- bazel-starlark -*-
# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Siso configuration for Android builds."""

load("@builtin//encoding.star", "json")
load("@builtin//lib/gn.star", "gn")
load("@builtin//struct.star", "module")
load("./config.star", "config")
load("./gn_logs.star", "gn_logs")

# TODO: crbug.com/323091468 - Propagate target android ABI and
# android SDK version from GN, and remove the hardcoded filegroups.
__archs = [
    "aarch64-linux-android",
    "arm-linux-androideabi",
    "i686-linux-android",
    "riscv64-linux-android",
    "x86_64-linux-android",
]

def __enabled(ctx):
    if "args.gn" in ctx.metadata:
        gn_args = gn.args(ctx)
        if gn_args.get("target_os") == '"android"':
            return True
    return False

def __filegroups(ctx):
    fg = {}
    for arch in __archs:
        api_level = gn_logs.read(ctx).get("android_ndk_api_level")
        if api_level:
            group = "third_party/android_toolchain/ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/%s/%s:link" % (arch, api_level)
            fg[group] = {
                "type": "glob",
                "includes": ["*"],
            }
    return fg

def __step_config(ctx, step_config):
    remote_run = True  # Turn this to False when you do file access trace.

    # Run static analysis steps locally when build server is enabled.
    # https://chromium.googlesource.com/chromium/src/+/main/docs/android_build_instructions.md#asynchronous-static-analysis
    remote_run_static_analysis = False
    if "args.gn" in ctx.metadata:
        gn_args = gn.args(ctx)

        # android_static_analysis = "build_server" by default.
        if gn_args.get("android_static_analysis") == '"on"':
            remote_run_static_analysis = True
        if gn_args.get("enable_kythe_annotations") == "true":
            # Remote Kythe annotations isn't supported.
            remote_run = False

    step_config["rules"].extend([
        # See also https://chromium.googlesource.com/chromium/src/build/+/HEAD/android/docs/java_toolchain.md
        {
            "name": "android/write_build_config",
            "command_prefix": "python3 ../../build/android/gyp/write_build_config.py",
            "handler": "android_write_build_config",
            "remote": remote_run,
            "canonicalize_dir": True,
            "timeout": "2m",
        },
        {
            "name": "android/ijar",
            "command_prefix": "python3 ../../build/android/gyp/ijar.py",
            "remote": remote_run,
            "canonicalize_dir": True,
            "timeout": "2m",
        },
        {
            "name": "android/turbine",
            "command_prefix": "python3 ../../build/android/gyp/turbine.py",
            "handler": "android_turbine",
            # TODO: crbug.com/396220357 - fix gn to remove unnecessary deps
            "exclude_input_patterns": [
                "*.a",
                "*.cc",
                "*.cpp",
                "*.h",
                "*.html",
                "*.inc",
                "*.info",
                "*.js",
                "*.map",
                "*.o",
                "*.pak",
                "*.proto",
                "*.sql",
                "*.stamp",
                "*.svg",
                "*.xml",
            ],
            "remote": remote_run,
            "platform_ref": "large",
            "canonicalize_dir": True,
            "timeout": "2m",
        },
        {
            "name": "android/compile_resources",
            "command_prefix": "python3 ../../build/android/gyp/compile_resources.py",
            "handler": "android_compile_resources",
            "exclude_input_patterns": [
                "*.a",
                "*.cc",
                "*.h",
                "*.inc",
                "*.info",
                "*.o",
                "*.pak",
                "*.sql",
            ],
            "remote": remote_run,
            "canonicalize_dir": True,
            "timeout": "5m",
        },
        {
            "name": "android/compile_java",
            "command_prefix": "python3 ../../build/android/gyp/compile_java.py",
            "handler": "android_compile_java",
            "exclude_input_patterns": [
                "*.a",
                "*.cc",
                "*.h",
                "*.inc",
                "*.info",
                "*.o",
                "*.pak",
                "*.sql",
            ],
            # Don't include files under --generated-dir.
            # This is probably optimization for local incrmental builds.
            # However, this is harmful for remote build cache hits.
            "ignore_extra_input_pattern": ".*srcjars.*\\.java",
            "ignore_extra_output_pattern": ".*srcjars.*\\.java",
            "remote": remote_run,
            "platform_ref": "large",
            "canonicalize_dir": True,
            "timeout": "2m",
        },
        {
            "name": "android/errorprone",
            "command_prefix": "python3 ../../build/android/gyp/errorprone.py",
            "handler": "android_compile_java",
            "exclude_input_patterns": [
                "*.a",
                "*.cc",
                "*.h",
                "*.inc",
                "*.info",
                "*.o",
                "*.pak",
                "*.sql",
            ],
            "remote": remote_run_static_analysis,
            "platform_ref": "large",
            "canonicalize_dir": True,
            # obj/chrome/android/chrome_java__errorprone.stamp step takes too
            # long.
            "timeout": "6m",
        },
        {
            "name": "android/compile_kt",
            "command_prefix": "python3 ../../build/android/gyp/compile_kt.py",
            "handler": "android_compile_java",
            "exclude_input_patterns": [
                "*.a",
                "*.cc",
                "*.h",
                "*.inc",
                "*.info",
                "*.o",
                "*.pak",
                "*.sql",
            ],
            # Don't include files under --generated-dir.
            # This is probably optimization for local incrmental builds.
            # However, this is harmful for remote build cache hits.
            "ignore_extra_input_pattern": ".*srcjars.*\\.java",
            "ignore_extra_output_pattern": ".*srcjars.*\\.java",
            "remote": remote_run,
            "platform_ref": "large",
            "canonicalize_dir": True,
            "timeout": "2m",
        },
        {
            "name": "android/dex",
            "command_prefix": "python3 ../../build/android/gyp/dex.py",
            "handler": "android_dex",
            # TODO(crbug.com/40270798): include only required jar, dex files in GN config.
            "indirect_inputs": {
                "includes": ["*.dex", "*.ijar.jar", "*.turbine.jar"],
            },
            "exclude_input_patterns": [
                "*.a",
                "*.cc",
                "*.h",
                "*.inc",
                "*.info",
                "*.o",
                "*.pak",
                "*.sql",
            ],
            # *.dex files are intermediate files used in incremental builds.
            # Fo remote actions, let's ignore them, assuming remote cache hits compensate.
            "ignore_extra_input_pattern": ".*\\.dex",
            "ignore_extra_output_pattern": ".*\\.dex",
            "remote": remote_run,
            "platform_ref": "large",
            "canonicalize_dir": True,
            "timeout": "2m",
        },
        {
            "name": "android/filter_zip",
            "command_prefix": "python3 ../../build/android/gyp/filter_zip.py",
            "remote": remote_run,
            "canonicalize_dir": True,
            "timeout": "2m",
        },
        {
            "name": "android/generate_resource_allowlist",
            "command_prefix": "python3 ../../tools/resources/generate_resource_allowlist.py",
            "indirect_inputs": {
                "includes": ["*.o", "*.a"],
            },
            # When remote linking without bytes enabled, .o, .a files don't
            # exist on the local file system.
            # This step also should run remortely to avoid downloading them.
            "remote": config.get(ctx, "remote-link"),
            "platform_ref": "large",
            "canonicalize_dir": True,
            "timeout": "2m",
        },
        {
            "name": "android/trace_event_bytecode_rewriter",
            "command_prefix": "python3 ../../build/android/gyp/trace_event_bytecode_rewriter.py",
            "handler": "android_trace_event_bytecode_rewriter",
            "canonicalize_dir": True,
            "remote": remote_run,
            "platform_ref": "large",
            "timeout": "10m",
        },
        {
            "name": "android/proguard/local",
            "command_prefix": "python3 ../../build/android/gyp/proguard.py",
            "action_outs": [
                # http://crbug.com/396004680#comment15: It slows down CQ build.
                # It's better to run it locally.
                "./obj/chrome/test/android_browsertests__apk/android_browsertests__apk.r8dex.jar",
            ],
            "remote": False,
        },
        {
            "name": "android/proguard",
            "command_prefix": "python3 ../../build/android/gyp/proguard.py",
            "handler": "android_proguard",
            "exclude_input_patterns": [
                "*.a",
                "*.cc",
                "*.h",
                "*.inc",
                "*.info",
                "*.o",
                "*.pak",
                "*.sql",
            ],
            "canonicalize_dir": True,
            "remote": remote_run,
            "platform_ref": "large",
            "timeout": "10m",
        },
        {
            "name": "android/trace_references",
            "command_prefix": "python3 ../../build/android/gyp/tracereferences.py",
            "handler": "android_trace_references",
            "exclude_input_patterns": [
                "*.a",
                "*.cc",
                "*.h",
                "*.inc",
                "*.info",
                "*.o",
                "*.pak",
                "*.sql",
            ],
            "canonicalize_dir": True,
            "remote": remote_run_static_analysis,
            "platform_ref": "large",
            "timeout": "10m",
        },
        {
            "name": "android/partition_action",
            "command_prefix": "python3 ../../build/extract_partition.py",
            "remote": config.get(ctx, "remote-link") or config.get(ctx, "builder"),
            "platform_ref": "large",
            "timeout": "4m",
        },
    ])
    return step_config

def __filearg(ctx, arg):
    fn = ""
    if arg.startswith("@FileArg("):
        f = arg.removeprefix("@FileArg(").removesuffix(")").split(":")
        fn = f[0].removesuffix("[]")  # [] suffix controls expand list?
        v = json.decode(str(ctx.fs.read(ctx.fs.canonpath(fn))))
        for k in f[1:]:
            v = v[k]
        arg = v
    if type(arg) == "string":
        if arg.startswith("["):
            return fn, json.decode(arg)
        return fn, [arg]
    return fn, arg

def __android_compile_resources_handler(ctx, cmd):
    # Script:
    #   https://crsrc.org/c/build/android/gyp/compile_resources.py
    # GN Config:
    #   https://crsrc.org/c/build/config/android/internal_rules.gni;l=2163;drc=1b15af251f8a255e44f2e3e3e7990e67e87dcc3b
    #   https://crsrc.org/c/build/config/android/system_image.gni;l=58;drc=39debde76e509774287a655285d8556a9b8dc634
    # Sample args:
    #   --aapt2-path ../../third_party/android_build_tools/aapt2/cipd/aapt2
    #   --android-manifest gen/chrome/android/trichrome_library_system_stub_apk__manifest.xml
    #   --arsc-package-name=org.chromium.trichromelibrary
    #   --arsc-path obj/chrome/android/trichrome_library_system_stub_apk.ap_
    #   --debuggable
    #   --dependencies-res-zip-overlays=@FileArg\(gen/chrome/android/webapk/shell_apk/maps_go_webapk.build_config.json:deps_info:dependency_zip_overlays\)
    #   --dependencies-res-zips=@FileArg\(gen/chrome/android/webapk/shell_apk/maps_go_webapk.build_config.json:deps_info:dependency_zips\)
    #   --depfile gen/chrome/android/webapk/shell_apk/maps_go_webapk__compile_resources.d
    #   --emit-ids-out=gen/chrome/android/webapk/shell_apk/maps_go_webapk__compile_resources.resource_ids
    #   --extra-res-packages=@FileArg\(gen/chrome/android/webapk/shell_apk/maps_go_webapk.build_config.json:deps_info:extra_package_names\)
    #   --include-resources(=)../../third_party/android_sdk/public/platforms/android-34/android.jar
    #   --info-path obj/chrome/android/webapk/shell_apk/maps_go_webapk.ap_.info
    #   --min-sdk-version=24
    #   --proguard-file obj/chrome/android/webapk/shell_apk/maps_go_webapk/maps_go_webapk.resources.proguard.txt
    #   --r-text-out gen/chrome/android/webapk/shell_apk/maps_go_webapk__compile_resources_R.txt
    #   --rename-manifest-package=org.chromium.trichromelibrary
    #   --srcjar-out gen/chrome/android/webapk/shell_apk/maps_go_webapk__compile_resources.srcjar
    #   --target-sdk-version=33
    #   --version-code 1
    #   --version-name Developer\ Build
    #   --webp-cache-dir=obj/android-webp-cache
    inputs = []
    for i, arg in enumerate(cmd.args):
        for k in ["--dependencies-res-zips=", "--dependencies-res-zip-overlays=", "--extra-res-packages="]:
            if arg.startswith(k):
                arg = arg.removeprefix(k)
                _, v = __filearg(ctx, arg)
                for f in v:
                    f = ctx.fs.canonpath(f)
                    inputs.append(f)
                    if k == "--dependencies-res-zips=" and ctx.fs.exists(f + ".info"):
                        inputs.append(f + ".info")

    ctx.actions.fix(
        inputs = cmd.inputs + inputs,
    )

def __android_compile_java_handler(ctx, cmd):
    # Script:
    #   https://crsrc.org/c/build/android/gyp/compile_java.py
    # GN Config:
    #   https://crsrc.org/c/build/config/android/internal_rules.gni;l=2995;drc=775b3a9ebccd468c79592dad43ef46632d3a411f
    # Sample args:
    #   --depfile=gen/chrome/android/chrome_test_java__compile_java.d
    #   --generated-dir=gen/chrome/android/chrome_test_java/generated_java
    #   --jar-path=obj/chrome/android/chrome_test_java.javac.jar
    #   --java-srcjars=\[\"gen/chrome/browser/tos_dialog_behavior_generated_enum.srcjar\",\ \"gen/chrome/android/chrome_test_java__assetres.srcjar\",\ \"gen/chrome/android/chrome_test_java.generated.srcjar\"\]
    #   --target-name //chrome/android:chrome_test_java__compile_java
    #   --classpath=@FileArg\(gen/chrome/android/chrome_test_java.build_config.json:android:sdk_interface_jars\)
    #   --header-jar obj/chrome/android/chrome_test_java.turbine.jar
    #   --classpath=\[\"obj/chrome/android/chrome_test_java.turbine.jar\"\]
    #   --classpath=@FileArg\(gen/chrome/android/chrome_test_java.build_config.json:deps_info:javac_full_interface_classpath\)
    #   --kotlin-jar-path=obj/chrome/browser/tabmodel/internal/java.kotlinc.jar
    #   --chromium-code=1
    #   --warnings-as-errors
    #   --jar-info-exclude-globs=\[\"\*/R.class\",\ \"\*/R\\\$\*.class\",\ \"\*/Manifest.class\",\ \"\*/Manifest\\\$\*.class\",\ \"\*/\*GEN_JNI.class\"\]
    #   --enable-errorprone
    #   @gen/chrome/android/chrome_test_java.sources

    out = cmd.outputs[0]
    outputs = [
        out + ".md5.stamp",
    ]

    inputs = []
    for i, arg in enumerate(cmd.args):
        for k in ["--classpath=", "--bootclasspath=", "--processorpath="]:
            if arg.startswith(k):
                arg = arg.removeprefix(k)
                fn, v = __filearg(ctx, arg)
                if fn:
                    inputs.append(ctx.fs.canonpath(fn))
                for f in v:
                    f, _, _ = f.partition(":")
                    inputs.append(ctx.fs.canonpath(f))

    ctx.actions.fix(
        inputs = cmd.inputs + inputs,
        outputs = cmd.outputs + outputs,
    )

def __android_dex_handler(ctx, cmd):
    out = cmd.outputs[0]
    inputs = []

    # Add __dex.desugardeps to the outputs.
    outputs = [
        out + ".md5.stamp",
    ]
    for i, arg in enumerate(cmd.args):
        if arg == "--desugar-dependencies":
            outputs.append(ctx.fs.canonpath(cmd.args[i + 1]))
        for k in ["--class-inputs=", "--bootclasspath=", "--classpath=", "--class-inputs-filearg=", "--dex-inputs-filearg="]:
            if arg.startswith(k):
                arg = arg.removeprefix(k)
                _, v = __filearg(ctx, arg)
                for f in v:
                    f, _, _ = f.partition(":")
                    f = ctx.fs.canonpath(f)
                    inputs.append(f)

    # TODO: dex.py takes --incremental-dir to reuse the .dex produced in a previous build.
    # Should remote dex action also take this?
    ctx.actions.fix(
        inputs = cmd.inputs + inputs,
        outputs = cmd.outputs + outputs,
    )

def __android_trace_event_bytecode_rewriter(ctx, cmd):
    # Sample command:
    # python3 ../../build/android/gyp/trace_event_bytecode_rewriter.py \
    #   --stamp obj/chrome/android/trichrome_chrome_bundle.trace_event_rewrite.stamp \
    #   --depfile gen/chrome/android/trichrome_chrome_bundle__trace_event_rewritten.d \
    #   --script bin/helper/trace_event_adder \
    #   --classpath @FileArg\(gen/chrome/android/trichrome_chrome_bundle.build_config.json:android:sdk_jars\) \
    #   --input-jars @FileArg\(gen/chrome/android/trichrome_chrome_bundle.build_config.json:deps_info:device_classpath\) \
    #   --output-jars @FileArg\(gen/chrome/android/trichrome_chrome_bundle.build_config.json:deps_info:trace_event_rewritten_device_classpath\)
    inputs = []
    outputs = []
    script = ""
    for i, arg in enumerate(cmd.args):
        if arg in ["--input-jars", "--classpath"]:
            fn, v = __filearg(ctx, cmd.args[i + 1])
            if fn:
                inputs.append(ctx.fs.canonpath(fn))
            for f in v:
                f, _, _ = f.partition(":")
                inputs.append(ctx.fs.canonpath(f))
            continue
        if arg == "--output-jars":
            fn, v = __filearg(ctx, cmd.args[i + 1])
            if fn:
                inputs.append(ctx.fs.canonpath(fn))
            for f in v:
                f, _, _ = f.partition(":")
                outputs.append(ctx.fs.canonpath(f))
            continue
        if arg == "--script":
            script = cmd.args[i + 1]
            continue

    # Find runtime jars for trace_event_adder
    if script == "bin/helper/trace_event_adder":
        trace_event_adder_json = json.decode(
            str(ctx.fs.read(ctx.fs.canonpath("gen/build/android/bytecode/trace_event_adder.build_config.json"))),
        )
        for path in trace_event_adder_json.get("host_classpath", []):
            inputs.append(ctx.fs.canonpath(path))

    ctx.actions.fix(
        inputs = cmd.inputs + inputs,
        outputs = cmd.outputs + outputs,
    )

def __android_proguard_handler(ctx, cmd):
    inputs = []
    outputs = []
    for i, arg in enumerate(cmd.args):
        for k in ["--proguard-configs=", "--input-paths=", "--feature-jars="]:
            if arg.startswith(k):
                arg = arg.removeprefix(k)
                fn, v = __filearg(ctx, arg)
                if fn:
                    inputs.append(ctx.fs.canonpath(fn))
                for f in v:
                    f, _, _ = f.partition(":")
                    inputs.append(ctx.fs.canonpath(f))
                break
        if arg in ["--sdk-jars", "--sdk-extension-jars"]:
            fn, v = __filearg(ctx, cmd.args[i + 1])
            if fn:
                inputs.append(ctx.fs.canonpath(fn))
            for f in v:
                f, _, _ = f.partition(":")
                inputs.append(ctx.fs.canonpath(f))
            continue
        if arg.startswith("--dex-dest="):
            arg = arg.removeprefix("--dex-dest=")
            fn, v = __filearg(ctx, arg)
            if fn:
                inputs.append(ctx.fs.canonpath(fn))
            for f in v:
                f, _, _ = f.partition(":")
                outputs.append(ctx.fs.canonpath(f))
            continue

    ctx.actions.fix(
        inputs = cmd.inputs + inputs,
        outputs = cmd.outputs + outputs,
    )

def __android_trace_references_handler(ctx, cmd):
    # Sample command:
    # python3 ../../build/android/gyp/tracereferences.py \
    #   --depfile gen/chrome/android/monochrome_public_bundle__dex.d \
    #   --tracerefs-json gen/chrome/android/monochrome_public_bundle__dex.tracerefs.json \
    #   --stamp obj/chrome/android/monochrome_public_bundle__dex.tracereferences.stamp --warnings-as-errors
    # Sample tracerefs.json:
    # {
    #   "r8jar": "../../third_party/r8/cipd/lib/r8.jar",
    #   "libs": [
    #     "../../clank/third_party/android_system_sdk/src/android_system.jar",
    #     "../../third_party/android_sdk/xr_extensions/com.android.extensions.xr.jar",
    #     "obj/third_party/android_sdk/window_extensions/androidx_window_extensions_java.javac.jar"
    #   ],
    #   "jobs": [
    #     {
    #       "name": "",
    #       "jars": [
    #         "obj/chrome/android/monochrome_public_bundle__base_bundle_module/monochrome_public_bundle__base_bundle_module.r8dex.jar",
    #         "obj/chrome/android/monochrome_public_bundle__chrome_bundle_module/monochrome_public_bundle__chrome_bundle_module.r8dex.jar",
    #         "obj/chrome/android/monochrome_public_bundle__dev_ui_bundle_module/monochrome_public_bundle__dev_ui_bundle_module.r8dex.jar",
    #         "obj/chrome/android/monochrome_public_bundle__stack_unwinder_bundle_module/monochrome_public_bundle__stack_unwinder_bundle_module.r8dex.jar",
    #         "obj/chrome/android/monochrome_public_bundle__test_dummy_bundle_module/monochrome_public_bundle__test_dummy_bundle_module.r8dex.jar"
    #       ]
    #     },
    #     {
    #       "name": "base",
    #       "jars": [
    #         "obj/chrome/android/monochrome_public_bundle__base_bundle_module/monochrome_public_bundle__base_bundle_module.r8dex.jar"
    #       ]
    #     }
    #   ]
    # }
    inputs = []
    for i, arg in enumerate(cmd.args):
        if arg == "--tracerefs-json":
            tracerefs_json = json.decode(str(ctx.fs.read(ctx.fs.canonpath(cmd.args[i + 1]))))
            break

    for lib in tracerefs_json.get("libs", []):
        inputs.append(ctx.fs.canonpath(lib))
    for job in tracerefs_json.get("jobs", []):
        for jar in job.get("jars", ""):
            inputs.append(ctx.fs.canonpath(jar))

    ctx.actions.fix(
        inputs = cmd.inputs + inputs,
    )

def __android_turbine_handler(ctx, cmd):
    inputs = []
    for i, arg in enumerate(cmd.args):
        for k in ["--classpath=", "--processorpath="]:
            if arg.startswith(k):
                arg = arg.removeprefix(k)
                _, v = __filearg(ctx, arg)
                for f in v:
                    f, _, _ = f.partition(":")
                    inputs.append(ctx.fs.canonpath(f))

    ctx.actions.fix(
        inputs = cmd.inputs + inputs,
    )

def __deps_configs(ctx, build_config_path, seen, inputs):
    if build_config_path in seen:
        return
    seen[build_config_path] = True
    params_path = build_config_path.replace(".build_config.json", ".params.json")
    inputs.append(build_config_path)
    inputs.append(params_path)
    build_config_data = json.decode(str(ctx.fs.read(build_config_path)))
    params_data = None

    # Entries can be in either .build_config.json or in .params.json.
    for configs_key in ["deps_configs", "public_deps_configs"]:
        sub_configs = build_config_data.get(configs_key)
        if not sub_configs:
            if not params_data:
                params_data = json.decode(str(ctx.fs.read(params_path)))
            sub_configs = params_data.get(configs_key, [])

        for f in sub_configs:
            __deps_configs(ctx, ctx.fs.canonpath(f), seen, inputs)

def __android_write_build_config_handler(ctx, cmd):
    # Script:
    #   https://crsrc.org/c/build/android/gyp/write_build_config.py
    # GN Config:
    #   https://crsrc.org/c/build/config/android/internal_rules.gni;l=122;drc=99e4f79301e108ea3d27ec84320f430490382587
    # Sample args:
    #   --depfile gen/third_party/android_deps/org_jetbrains_kotlinx_kotlinx_metadata_jvm_java__build_config_crbug_908819.d
    #   --params gen/third_party/android_deps/org_jetbrains_kotlinx_kotlinx_metadata_jvm_java.params.json
    inputs = []
    seen = {}
    for i, arg in enumerate(cmd.args):
        if arg == "--params":
            params_path = ctx.fs.canonpath(cmd.args[i + 1])
            output_build_config_path = params_path.replace(".params.json", ".build_config.json")
            v = json.decode(str(ctx.fs.read(params_path)))
            path = v.get("shared_libraries_runtime_deps_file")
            if path:
                inputs.append(ctx.fs.canonpath(path))
            path = v.get("secondary_abi_shared_libraries_runtime_deps_file")
            if path:
                inputs.append(ctx.fs.canonpath(path))
            for k in ["apk_under_test_config", "base_module_config", "parent_module_config", "suffix_apk_assets_used_by_config"]:
                path = v.get(k)
                if path:
                    path = ctx.fs.canonpath(path)
                    if path != output_build_config_path:
                        __deps_configs(ctx, path, seen, inputs)
            for k in ["deps_configs", "public_deps_configs", "processor_configs", "module_configs"]:
                for path in v.get(k, []):
                    path = ctx.fs.canonpath(path)
                    __deps_configs(ctx, path, seen, inputs)

    ctx.actions.fix(inputs = cmd.inputs + inputs)

__handlers = {
    "android_compile_java": __android_compile_java_handler,
    "android_compile_resources": __android_compile_resources_handler,
    "android_dex": __android_dex_handler,
    "android_trace_event_bytecode_rewriter": __android_trace_event_bytecode_rewriter,
    "android_proguard": __android_proguard_handler,
    "android_trace_references": __android_trace_references_handler,
    "android_turbine": __android_turbine_handler,
    "android_write_build_config": __android_write_build_config_handler,
}

android = module(
    "android",
    enabled = __enabled,
    archs = __archs,
    step_config = __step_config,
    filegroups = __filegroups,
    handlers = __handlers,
)
