/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "perfetto/base/build_config.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/version.h"
#include "src/protozero/descriptor_diff/descriptor_diff.h"

// For dup().
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace protozero {
namespace {

using ::perfetto::base::ReadFile;
using ::perfetto::base::StatusOr;

const char kUsage[] =
    R"(Usage: protozero_descriptor_diff [options]

-m --minuend:    Required. Path to a protobuf descriptor (serialized
                 FileDescriptorSet).
-s --subtrahend: Required. Path to a protobuf descriptor (serialized
                 FileDescriptorSet).
-o --out:        Path where the output will be written.

Computes the set difference of two protobuf descriptor. Creates a descriptor
with all the protos that are in minuend but are not in subtrahend.

Example usage:

# Creates a proto descriptor with all the protos that are in
  chrome_track_event.descriptor, but are not in trace.descriptor:

  protozero_descriptor_diff \
    --subtrahend trace.descriptor \
    --minuend chrome_track_event.descriptor \
    --out /tmp/chrome_track_event_extension.descriptor
)";

int DescriptorDiffMain(int argc, char** argv) {
  static const option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"version", no_argument, nullptr, 'V'},
      {"minuend", required_argument, nullptr, 'm'},
      {"subtrahend", required_argument, nullptr, 's'},
      {"out", required_argument, nullptr, 'o'},
      {nullptr, 0, nullptr, 0}};

  const char* out = nullptr;
  const char* minuend = nullptr;
  const char* subtrahend = nullptr;

  for (;;) {
    int option = getopt_long(argc, argv, "hm:s:o:V:", long_options, nullptr);

    if (option == -1)
      break;  // EOF.

    if (option == 'V') {
      printf("%s\n", perfetto::base::GetVersionString());
      exit(0);
    }

    if (option == 'm') {
      minuend = optarg;
      continue;
    }

    if (option == 's') {
      subtrahend = optarg;
      continue;
    }

    if (option == 'o') {
      out = optarg;
      continue;
    }

    if (option == 'h') {
      fprintf(stdout, kUsage);
      return 0;
    }

    fprintf(stderr, kUsage);
    return 1;
  }

  if (!minuend || !subtrahend) {
    fprintf(stderr, kUsage);
    return 1;
  }

  perfetto::base::ScopedFile out_fd;
  if (out == nullptr || !strcmp(out, "-")) {
    out_fd.reset(dup(fileno(stdout)));
  } else {
    out_fd = perfetto::base::OpenFile(out, O_RDWR | O_CREAT | O_TRUNC, 0600);
  }

  if (!out_fd) {
    PERFETTO_ELOG("Cannot open output file %s", out);
    return 1;
  }

  std::string minuend_data;
  if (!ReadFile(minuend, &minuend_data)) {
    PERFETTO_ELOG("Could not open message file %s", minuend);
    return 1;
  }

  std::string subtrahend_data;
  if (!ReadFile(subtrahend, &subtrahend_data)) {
    PERFETTO_ELOG("Could not open message file %s", subtrahend);
    return 1;
  }

  StatusOr<std::string> res = DescriptorDiff(minuend_data, subtrahend_data);
  if (!res.ok()) {
    PERFETTO_ELOG("Error diffing: %s", res.status().c_message());
    return 1;
  }

  if (perfetto::base::WriteAll(*out_fd, res->data(), res->size()) == -1) {
    PERFETTO_ELOG("Error writing to output file");
    return 1;
  }

  return 0;
}

}  // namespace
}  // namespace protozero

int main(int argc, char* argv[]) {
  return protozero::DescriptorDiffMain(argc, argv);
}
