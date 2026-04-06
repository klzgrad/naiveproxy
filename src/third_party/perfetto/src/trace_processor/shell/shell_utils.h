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

#ifndef SRC_TRACE_PROCESSOR_SHELL_SHELL_UTILS_H_
#define SRC_TRACE_PROCESSOR_SHELL_SHELL_UTILS_H_

#include <string>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_processor.h"

namespace perfetto::trace_processor {

bool StderrSupportsColors();

base::Status PrintStats(TraceProcessor* tp);

base::Status ExportTraceToDatabase(TraceProcessor* trace_processor,
                                   const std::string& output_name);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_SHELL_SHELL_UTILS_H_
