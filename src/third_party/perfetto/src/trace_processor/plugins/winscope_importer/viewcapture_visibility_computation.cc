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

#include "src/trace_processor/plugins/winscope_importer/viewcapture_visibility_computation.h"

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
  std::unordered_map<int32_t, bool> visibility_flag_set;
  for (auto it = views_top_to_bottom_.begin(); it != views_top_to_bottom_.end();
       it++) {
    const auto& view = *it;
    auto node_id = view.id();

    auto visibility_set = view.visibility() == IS_VISIBLE;
    auto parent = visibility_flag_set.find(view.parent_id());
    if (visibility_set && parent != visibility_flag_set.end()) {
      visibility_set = parent->second;
    }
    visibility_flag_set[node_id] = visibility_set;

    auto is_visible = visibility_set && view.width() > 0 && view.height() > 0;
    computed_visibility[node_id] = is_visible;
  }
  return computed_visibility;
}
}  // namespace perfetto::trace_processor::winscope::viewcapture
