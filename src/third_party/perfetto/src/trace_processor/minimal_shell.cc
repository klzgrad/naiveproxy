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

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/read_trace.h"
#include "perfetto/trace_processor/trace_processor.h"

namespace perfetto::trace_processor {
namespace {

// This binary exists just for the purpose of debugging the binary size of
// trace processor. To that end, we just run some basic trace processor
// functions to ensure that the linker does not strip the TP symbols.
base::Status MinimalMain(int, char**) {
  std::unique_ptr<TraceProcessor> tp = TraceProcessor::CreateInstance({});
  RETURN_IF_ERROR(tp->Parse(std::unique_ptr<uint8_t[]>(new uint8_t[0]), 0));
  RETURN_IF_ERROR(tp->NotifyEndOfFile());

  auto it = tp->ExecuteQuery("SELECT id FROM slice");
  while (it.Next()) {
    SqlValue value = it.Get(0);
    fprintf(stderr, "%" PRId64, value.AsLong());
  }
  return it.Status();
}

}  // namespace
}  // namespace perfetto::trace_processor

int main(int argc, char** argv) {
  auto status = perfetto::trace_processor::MinimalMain(argc, argv);
  if (!status.ok()) {
    fprintf(stderr, "%s\n", status.c_message());
    return 1;
  }
  return 0;
}
