# Copyright 2022 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

licenses(["notice"])

cc_library(
    name = "zlib",
    srcs = glob(
        include = [
            "*.c",
            "*.h",
        ],
        exclude = [
            "zlib.h",
            "zconf.h",
        ],
    ),
    hdrs = [
        "zconf.h",
        "zlib.h",
    ],
    copts = ["-Wno-implicit-function-declaration"],
    visibility = ["//visibility:public"],
)
