/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/version.h"
#include "src/tools/tracing_proto_extensions.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
#include <zlib.h>
#endif

namespace perfetto {
namespace gen_proto_extensions {
namespace {

const char kUsage[] =
    R"(Usage: tracing_proto_extensions [options]

Reads a track_event_extensions.json registry, compiles referenced .proto files,
validates extension field numbers, and generates a merged FileDescriptorSet.

-j, --json:            Path to the root track_event_extensions.json file.
-I, --proto_path:      Proto include directory (can be specified multiple times).
-o, --descriptor-out:  Output path for the binary FileDescriptorSet.
    --gzip:            Gzip-compress the output.
-h, --help:            Show this help.
-v, --version:         Show version.

Example:

  tracing_proto_extensions \
    --json protos/perfetto/trace/track_event/track_event_extensions.json \
    -I . \
    --descriptor-out /tmp/extensions.descriptor

  tracing_proto_extensions \
    --json protos/perfetto/trace/track_event/track_event_extensions.json \
    -I . \
    --descriptor-out /tmp/extensions.descriptor.gz --gzip
)";

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
std::vector<uint8_t> GzipCompress(const std::vector<uint8_t>& input) {
  z_stream zs{};
  // windowBits=15 + 16 for gzip encoding.
  if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, MAX_MEM_LEVEL,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    PERFETTO_FATAL("deflateInit2 failed");
  }
  zs.avail_in = static_cast<uInt>(input.size());
  zs.next_in = const_cast<Bytef*>(input.data());

  std::vector<uint8_t> output;
  output.resize(deflateBound(&zs, static_cast<uLong>(input.size())));
  zs.avail_out = static_cast<uInt>(output.size());
  zs.next_out = output.data();

  int ret = deflate(&zs, Z_FINISH);
  PERFETTO_CHECK(ret == Z_STREAM_END);
  output.resize(zs.total_out);
  deflateEnd(&zs);
  return output;
}
#endif

int Main(int argc, char** argv) {
  static const option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"version", no_argument, nullptr, 'v'},
      {"json", required_argument, nullptr, 'j'},
      {"proto_path", required_argument, nullptr, 'I'},
      {"descriptor-out", required_argument, nullptr, 'o'},
      {"gzip", no_argument, nullptr, 'g'},
      {nullptr, 0, nullptr, 0}};

  std::string json_path;
  std::vector<std::string> proto_paths;
  std::string output_path;
  bool use_gzip = false;
  bool has_args = false;

  for (;;) {
    int option = getopt_long(argc, argv, "hvj:I:o:", long_options, nullptr);
    if (option == -1)
      break;

    if (option == 'v') {
      printf("%s\n", base::GetVersionString());
      return 0;
    }
    if (option == 'h') {
      fprintf(stdout, "%s", kUsage);
      return 0;
    }

    has_args = true;

    if (option == 'j') {
      json_path = optarg;
      continue;
    }
    if (option == 'I') {
      proto_paths.emplace_back(optarg);
      continue;
    }
    if (option == 'o') {
      output_path = optarg;
      continue;
    }
    if (option == 'g') {
      use_gzip = true;
      continue;
    }
    fprintf(stderr, "%s", kUsage);
    return 1;
  }

  if (!has_args) {
    fprintf(stdout, "%s", kUsage);
    return 1;
  }

  if (json_path.empty()) {
    PERFETTO_ELOG("--json is required");
    return 1;
  }
  if (output_path.empty()) {
    PERFETTO_ELOG("--descriptor-out is required");
    return 1;
  }
  if (proto_paths.empty()) {
    PERFETTO_ELOG("At least one -I proto_path is required");
    return 1;
  }

  // Derive root_dir from json_path (the directory containing the JSON).
  // Actually, we use the first -I path as the root_dir for resolving
  // relative paths in the JSON.
  const std::string& root_dir = proto_paths[0];

  auto result = GenerateExtensionDescriptors(json_path, proto_paths, root_dir);
  if (!result.ok()) {
    PERFETTO_ELOG("Error: %s", result.status().message().c_str());
    return 1;
  }

  std::vector<uint8_t> output = std::move(*result);
  PERFETTO_ILOG("Generated FileDescriptorSet: %zu bytes", output.size());

  if (use_gzip) {
#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
    size_t uncompressed_size = output.size();
    output = GzipCompress(output);
    PERFETTO_ILOG("Gzip compressed: %zu -> %zu bytes", uncompressed_size,
                  output.size());
#else
    PERFETTO_ELOG("--gzip requested but PERFETTO_ZLIB is not enabled");
    return 1;
#endif
  }

  base::ScopedFile out_fd(
      base::OpenFile(output_path, O_CREAT | O_WRONLY | O_TRUNC, 0664));
  if (!out_fd) {
    PERFETTO_ELOG("Failed to open output file: %s", output_path.c_str());
    return 1;
  }
  base::WriteAll(*out_fd, output.data(), output.size());

  return 0;
}

}  // namespace
}  // namespace gen_proto_extensions
}  // namespace perfetto

int main(int argc, char** argv) {
  return perfetto::gen_proto_extensions::Main(argc, argv);
}
