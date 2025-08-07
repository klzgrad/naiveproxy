/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef SRC_TRACECONV_TRACE_TO_HPROF_H_
#define SRC_TRACECONV_TRACE_TO_HPROF_H_

#include <iostream>
#include <vector>
#include "perfetto/trace_processor/trace_processor.h"

namespace perfetto {
namespace trace_to_text {

int TraceToHprof(trace_processor::TraceProcessor* tp,
                 std::ostream* output,
                 uint64_t pid,
                 uint64_t timestamp);

int TraceToHprof(std::istream* input,
                 std::ostream* output,
                 uint64_t pid = 0,
                 std::vector<uint64_t> timestamps = {});

}  // namespace trace_to_text
}  // namespace perfetto

#endif  // SRC_TRACECONV_TRACE_TO_HPROF_H_
