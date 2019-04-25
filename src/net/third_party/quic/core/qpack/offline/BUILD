licenses(["notice"])  # Chromium

package(default_visibility = ["//third_party/quic:quic_users"])

load("//gfe/gfe2/tools:build_defs.bzl", "cc_test_library")

cc_binary(
    name = "qpack_offline_decoder",
    testonly = 1,
    srcs = ["qpack_offline_decoder_bin.cc"],
    deps = [
        ":qpack_offline_decoder_lib",
        "//base",
        "//file/localfile",
        "//third_party/quic/core/qpack:qpack_decoder_test_utils_lib",
        "//third_party/quic/core/qpack:qpack_test_utils_lib",
        "//third_party/quic/platform/api:quic_logging_lib",
        "//third_party/quic/platform/api:quic_string_piece_lib",
    ],
)

cc_test_library(
    name = "qpack_offline_decoder_lib",
    srcs = ["qpack_offline_decoder.cc"],
    hdrs = ["qpack_offline_decoder.h"],
    deps = [
        "//third_party/quic/core:quic_types_lib",
        "//third_party/quic/core/qpack:qpack_decoder_lib",
        "//third_party/quic/core/qpack:qpack_decoder_test_utils_lib",
        "//third_party/quic/core/qpack:qpack_test_utils_lib",
        "//third_party/quic/platform/api:quic_endian_lib",
        "//third_party/quic/platform/api:quic_file_utils_lib",
        "//third_party/quic/platform/api:quic_logging_lib",
        "//third_party/quic/platform/api:quic_string_lib",
        "//third_party/quic/platform/api:quic_string_piece_lib",
        "//third_party/quic/platform/api:quic_text_utils_lib",
        "//third_party/spdy/core:spdy_header_block_lib",
    ],
)
