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

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "src/trace_redaction/trace_redaction_framework.h"
#include "src/trace_redaction/trace_redactor.h"
#include "src/trace_redaction/verify_integrity.h"

namespace perfetto::trace_redaction {

// Builds and runs a trace redactor.
static base::Status Main(std::string_view input,
                         std::string_view output,
                         std::string_view package_name) {
  TraceRedactor::Config config;
  auto redactor = TraceRedactor::CreateInstance(config);

  Context context;
  context.package_name = package_name;

  return redactor->Redact(input, output, &context);
}

}  // namespace perfetto::trace_redaction

int main(int argc, char** argv) {
  constexpr int kSuccess = 0;
  constexpr int kFailure = 1;
  constexpr int kInvalidArgs = 2;

  if (argc != 4) {
    PERFETTO_ELOG(
        "Invalid arguments: %s <input file> <output file> <package name>",
        argv[0]);
    return kInvalidArgs;
  }

  auto result = perfetto::trace_redaction::Main(argv[1], argv[2], argv[3]);

  if (result.ok()) {
    return kSuccess;
  }

  PERFETTO_ELOG("Unexpected error: %s", result.c_message());
  return kFailure;
}
