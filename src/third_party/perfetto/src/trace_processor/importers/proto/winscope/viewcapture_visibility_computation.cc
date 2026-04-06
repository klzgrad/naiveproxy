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

#include "src/trace_processor/importers/proto/winscope/viewcapture_visibility_computation.h"

namespace perfetto::trace_processor::winscope::viewcapture {

// Consts only used by this computation.

namespace {
const auto IS_VISIBLE = 0;
}

VisibilityComputation::VisibilityComputation(
    const std::vector<ViewDecoder>& views_top_to_bottom)
    : views_top_to_bottom_(views_top_to_bottom) {}

std::unordered_map<int32_t, bool> VisibilityComputation::Compute() {
  std::unordered_map<int32_t, bool> computed_visibility;
  for (auto it = views_top_to_bottom_.begin(); it != views_top_to_bottom_.end();
       it++) {
    const auto& view = *it;
    auto node_id = view.id();
    auto is_visible = view.visibility() == IS_VISIBLE;

    auto parent = computed_visibility.find(view.parent_id());
    if (is_visible && parent != computed_visibility.end()) {
      is_visible = parent->second;
    }

    computed_visibility[node_id] = is_visible;
  }
  return computed_visibility;
}
}  // namespace perfetto::trace_processor::winscope::viewcapture
