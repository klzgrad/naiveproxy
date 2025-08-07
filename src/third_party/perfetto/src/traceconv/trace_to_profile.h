/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef SRC_TRACECONV_TRACE_TO_PROFILE_H_
#define SRC_TRACECONV_TRACE_TO_PROFILE_H_

#include <cstdint>
#include <iostream>
#include <vector>

namespace perfetto {
namespace trace_to_text {

// 0: success
int TraceToHeapProfile(std::istream* input,
                       std::ostream* output,
                       uint64_t pid,
                       std::vector<uint64_t> timestamps,
                       bool annotate_frames);

// 0: success
int TraceToPerfProfile(std::istream* input,
                       std::ostream* output,
                       uint64_t pid,
                       std::vector<uint64_t> timestamps,
                       bool annotate_frames);

// 0: success
int TraceToJavaHeapProfile(std::istream* input,
                           std::ostream* output,
                           uint64_t pid,
                           const std::vector<uint64_t>& timestamps,
                           bool annotate_frames);

}  // namespace trace_to_text
}  // namespace perfetto

#endif  // SRC_TRACECONV_TRACE_TO_PROFILE_H_
