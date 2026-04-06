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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_VISIBILITY_COMPUTATION_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_VISIBILITY_COMPUTATION_H_

#include <unordered_map>
#include <unordered_set>
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/android/viewcapture.pbzero.h"

namespace perfetto::trace_processor::winscope::viewcapture {

namespace {
using ViewDecoder = protos::pbzero::ViewCapture::View::Decoder;
}

// Computes visibility for every view in hierarchy, based on its properties and
// position in the hierarchy.

class VisibilityComputation {
 public:
  explicit VisibilityComputation(
      const std::vector<ViewDecoder>& views_top_to_bottom);

  std::unordered_map<int32_t, bool> Compute();

 private:
  const std::vector<ViewDecoder>& views_top_to_bottom_;
};

}  // namespace perfetto::trace_processor::winscope::viewcapture

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_VISIBILITY_COMPUTATION_H_
