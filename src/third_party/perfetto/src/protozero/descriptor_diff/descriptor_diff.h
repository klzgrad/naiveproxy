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

#ifndef SRC_PROTOZERO_DESCRIPTOR_DIFF_DESCRIPTOR_DIFF_H_
#define SRC_PROTOZERO_DESCRIPTOR_DIFF_DESCRIPTOR_DIFF_H_

#include <string>
#include <string_view>

#include "perfetto/ext/base/status_or.h"

namespace protozero {

// Returns the set difference of two proto descriptors.
//
// Takes two serialized binary `FileDescriptorSet`s and returns a serialized
// binary `FileDescriptorSet` that contains all protos that are in `minuend` but
// not in `subtrahend`.
//
// Example:
//
// minuend:
//
// ```textproto
// file {
//   name: "protos/perfetto/trace/android/android_trace_packet.proto"
//   ...
// }
// file {
//   name: "protos/perfetto/trace/trace_packet.proto"
//   ...
// }
// ```
//
// subtrahend:
//
// ```textproto
// file {
//   name: "protos/perfetto/trace/trace_packet.proto"
//   ...
// }
// ```
//
// output:
// ```textproto
// file {
//   name: "protos/perfetto/trace/android/android_trace_packet.proto"
//   ...
// }
// ```
perfetto::base::StatusOr<std::string> DescriptorDiff(
    std::string_view minuend,
    std::string_view subtrahend);

}  // namespace protozero

#endif  // SRC_PROTOZERO_DESCRIPTOR_DIFF_DESCRIPTOR_DIFF_H_
