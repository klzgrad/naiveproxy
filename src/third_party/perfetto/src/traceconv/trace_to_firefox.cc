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

#include "src/traceconv/trace_to_firefox.h"

#include <ios>
#include <istream>
#include <memory>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/traceconv/utils.h"

namespace perfetto {
namespace trace_to_text {
namespace {

void ExportFirefoxProfile(trace_processor::TraceProcessor& tp,
                          std::ostream* output) {
  auto it = tp.ExecuteQuery(R"(
      INCLUDE PERFETTO MODULE export.to_firefox_profile;
      SELECT CAST(export_to_firefox_profile() AS BLOB);
    )");
  PERFETTO_CHECK(it.Next());

  it.Get(0).AsBytes();
  output->write(reinterpret_cast<const char*>(it.Get(0).AsBytes()),
                static_cast<std::streamsize>(it.Get(0).bytes_count));

  PERFETTO_CHECK(!it.Next());
  PERFETTO_CHECK(it.Status().ok());
}

std::unique_ptr<trace_processor::TraceProcessor> LoadTrace(
    std::istream* input) {
  trace_processor::Config config;
  std::unique_ptr<trace_processor::TraceProcessor> tp =
      trace_processor::TraceProcessor::CreateInstance(config);

  if (!ReadTraceUnfinalized(tp.get(), input)) {
    return nullptr;
  }
  if (auto status = tp->NotifyEndOfFile(); !status.ok()) {
    return nullptr;
  }
  return tp;
}

}  // namespace

bool TraceToFirefoxProfile(std::istream* input, std::ostream* output) {
  auto tp = LoadTrace(input);
  if (!tp) {
    return false;
  }
  ExportFirefoxProfile(*tp, output);
  return true;
}

}  // namespace trace_to_text
}  // namespace perfetto
