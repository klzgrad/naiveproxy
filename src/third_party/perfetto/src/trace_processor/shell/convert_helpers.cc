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

#include "src/trace_processor/shell/convert_helpers.h"

#include <cstdint>
#include <ios>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/utils.h"
#include "src/protozero/text_to_proto/text_to_proto.h"
#include "src/traceconv/android_extension.descriptor.h"
#include "src/traceconv/trace.descriptor.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <fcntl.h>
#include <io.h>
#endif

namespace perfetto::trace_processor::shell {

base::Status OpenConversionInput(const std::string& path,
                                 std::ifstream* owned_file,
                                 std::istream** out_stream) {
  if (!path.empty() && path != "-") {
    owned_file->open(path, std::ios_base::in | std::ios_base::binary);
    if (!owned_file->is_open())
      return base::ErrStatus("Could not open %s", path.c_str());
    *out_stream = owned_file;
    return base::OkStatus();
  }
  if (base::IsTty(stdin)) {
    return base::ErrStatus(
        "Reading from stdin but it's connected to a TTY. Either pass a file "
        "path on the command line or pipe data into stdin.");
  }
  *out_stream = &std::cin;
  return base::OkStatus();
}

base::Status OpenConversionOutput(const std::string& path,
                                  bool binary_output,
                                  std::ofstream* owned_file,
                                  std::ostream** out_stream) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // We don't want the runtime to replace "\n" with "\r\n" on `std::cout`.
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  if (!path.empty() && path != "-") {
    owned_file->open(path, std::ios_base::out | std::ios_base::trunc |
                               std::ios_base::binary);
    if (!owned_file->is_open())
      return base::ErrStatus("Could not open %s", path.c_str());
    *out_stream = owned_file;
    return base::OkStatus();
  }
  if (binary_output && base::IsTty(stdout)) {
    return base::ErrStatus(
        "Refusing to write binary output to stdout as it's connected to a TTY "
        "(this would corrupt your terminal). Either pass an output file path "
        "on the command line or redirect stdout to a file or pipe.");
  }
  *out_stream = &std::cout;
  return base::OkStatus();
}

base::Status TextToTrace(std::istream* input, std::ostream* output) {
  std::string trace_text(std::istreambuf_iterator<char>{*input},
                         std::istreambuf_iterator<char>{});
  std::vector<uint8_t> descriptors;
  descriptors.reserve(kTraceDescriptor.size() +
                      kAndroidExtensionDescriptor.size());
  descriptors.insert(descriptors.end(), kTraceDescriptor.begin(),
                     kTraceDescriptor.end());
  descriptors.insert(descriptors.end(), kAndroidExtensionDescriptor.begin(),
                     kAndroidExtensionDescriptor.end());
  auto proto_status =
      protozero::TextToProto(descriptors.data(), descriptors.size(),
                             ".perfetto.protos.Trace", "trace", trace_text);
  if (!proto_status.ok()) {
    return base::ErrStatus("failed to parse trace text: %s",
                           proto_status.status().c_message());
  }
  const std::vector<uint8_t>& trace_proto = proto_status.value();
  output->write(reinterpret_cast<const char*>(trace_proto.data()),
                static_cast<int64_t>(trace_proto.size()));
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::shell
