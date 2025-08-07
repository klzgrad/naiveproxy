/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/traceconv/trace_unpack.h"

#include <iostream>
#include <iterator>
#include <memory>
#include <vector>

#include "perfetto/trace_processor/read_trace.h"
#include "src/traceconv/utils.h"

namespace perfetto {
namespace trace_to_text {

// Naive: puts multiple copies of the trace in memory, but good enough for
// manual workflows.
bool UnpackCompressedPackets(std::istream* input, std::ostream* output) {
  std::vector<char> packed(std::istreambuf_iterator<char>{*input},
                           std::istreambuf_iterator<char>{});
  std::vector<uint8_t> unpacked;
  auto status = trace_processor::DecompressTrace(
      reinterpret_cast<uint8_t*>(packed.data()), packed.size(), &unpacked);
  if (!status.ok())
    return false;

  TraceWriter trace_writer(output);
  trace_writer.Write(reinterpret_cast<char*>(unpacked.data()), unpacked.size());
  return true;
}

}  // namespace trace_to_text
}  // namespace perfetto
