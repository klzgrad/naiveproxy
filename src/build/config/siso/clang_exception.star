# -*- bazel-starlark -*-
# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""workaround for exceptional clang actions. e.g. exit=137 (OOM?),
 or DeadlineExceeded.
"""

load("@builtin//runtime.star", "runtime")
load("@builtin//struct.star", "module")

def __step_config(ctx, step_config):
    exceptions = [
        {
            # TODO: crbug.com/380755128 - Make each compile unit smaller.
            "name": "fuzzer_large_compile",
            "action_outs": [
                # keep-sorted start
                "./obj/chrome/test/fuzzing/htmlfuzzer_proto_gen/htmlfuzzer_sub.pb.o",
                "./obj/chrome/test/fuzzing/jsfuzzer/jsfuzzer.o",
                "./obj/chrome/test/fuzzing/jsfuzzer_proto_gen/jsfuzzer.pb.o",
                "./obj/chrome/test/fuzzing/jsfuzzer_proto_gen/jsfuzzer_sub.pb.o",
                "./obj/chrome/test/fuzzing/renderer_fuzzing/renderer_in_process_mojolpm_fuzzer/renderer_in_process_mojolpm_fuzzer.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidl_fuzzer_grammar/webidl_fuzzer_grammar.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidl_fuzzer_grammar_proto_gen/webidl_fuzzer_grammar.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidl_in_process_fuzzer/webidl_in_process_fuzzer.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer/webidlfuzzer.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer/webidlfuzzer_sub0.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer/webidlfuzzer_sub9.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub0.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub1.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub10.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub11.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub2.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub3.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub4.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub5.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub6.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub7.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub8.pb.o",
                "./obj/chrome/test/fuzzing/webidl_fuzzing/webidlfuzzer_proto_gen/webidlfuzzer_sub9.pb.o",
                # keep-sorted end
            ],
            "timeout": "15m",
            # need 9G for debug build
            "use_large": True,
        },
    ]
    new_rules = []
    for rule in step_config["rules"]:
        if not rule["name"].endswith("/cxx"):
            new_rules.append(rule)
            continue
        if "action_outs" in rule:
            fail("unexpeced \"action_outs\" in cxx rule %s" % rule["name"])
        for ex in exceptions:
            r = {}
            r.update(rule)
            r["name"] += "/exception/" + ex["name"]
            outs = ex["action_outs"]
            if runtime.os == "windows":
                outs = [obj.removesuffix(".o") + ".obj" for obj in outs if obj.startswith("./obj/")]
            r["action_outs"] = outs
            if "timeout" in ex:
                r["timeout"] = ex["timeout"]
            if "use_large" in ex and ex["use_large"]:
                # use `_large` variant of platform if it doesn't use default platform,
                # i.e. mac/win case.
                if "platform_ref" in r:
                    r["platform_ref"] = r["platform_ref"] + "_large"
                else:
                    r["platform_ref"] = "large"
            if r.get("handler") == "rewrite_rewrapper":
                r["handler"] = "rewrite_rewrapper_large"
            new_rules.append(r)
        new_rules.append(rule)
    step_config["rules"] = new_rules
    return step_config

clang_exception = module(
    "clang_exception",
    step_config = __step_config,
)
