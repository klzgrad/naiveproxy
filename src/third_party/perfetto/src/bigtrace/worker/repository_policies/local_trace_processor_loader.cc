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
#include "src/bigtrace/worker/repository_policies/local_trace_processor_loader.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/trace_processor/read_trace.h"

namespace perfetto::bigtrace {

base::StatusOr<std::unique_ptr<trace_processor::TraceProcessor>>
LocalTraceProcessorLoader::LoadTraceProcessor(const std::string& path) {
  trace_processor::Config config;
  std::unique_ptr<trace_processor::TraceProcessor> tp =
      trace_processor::TraceProcessor::CreateInstance(config);

  RETURN_IF_ERROR(trace_processor::ReadTrace(tp.get(), path.c_str()));

  return tp;
}

}  // namespace perfetto::bigtrace
