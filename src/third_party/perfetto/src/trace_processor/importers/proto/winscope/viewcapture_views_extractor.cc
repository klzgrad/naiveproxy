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

#include "src/trace_processor/importers/proto/winscope/viewcapture_views_extractor.h"

#include <unordered_map>
#include <utility>

namespace perfetto::trace_processor::winscope::viewcapture {

namespace {
const auto ROOT_PARENT_ID = -1;

void ExtractNodeIdsDfs(
    int32_t current_id,
    const std::unordered_map<int32_t, std::vector<int32_t>>&
        child_ids_by_parent,
    const std::unordered_map<int32_t, ViewDecoder>& views_by_id,
    std::vector<int32_t>& node_ids_dfs) {
  auto view_pos = views_by_id.find(current_id);
  if (view_pos == views_by_id.end()) {
    return;
  }
  node_ids_dfs.push_back(view_pos->second.id());

  auto child_it = child_ids_by_parent.find(current_id);
  if (child_it != child_ids_by_parent.end()) {
    const std::vector<int32_t>& child_ids = child_it->second;
    for (int32_t child_id : child_ids) {
      ExtractNodeIdsDfs(child_id, child_ids_by_parent, views_by_id,
                        node_ids_dfs);
    }
  }
}
}  // namespace

// Returns a vector of views in top-to-bottom drawing order (z order), so
// we can determine visibility based on parents.

std::vector<ViewDecoder> ExtractViewsTopToBottom(
    const SnapshotDecoder& snapshot_decoder) {
  int32_t root_node_id = 0;
  std::unordered_map<int32_t, std::vector<int32_t>> child_ids_by_parent;
  std::unordered_map<int32_t, ViewDecoder> views_by_id;

  for (auto it = snapshot_decoder.views(); it; ++it) {
    ViewDecoder view(*it);
    auto node_id = view.id();

    if (view.parent_id() == ROOT_PARENT_ID) {
      root_node_id = node_id;
    } else {
      const auto parent = view.parent_id();
      child_ids_by_parent[parent].emplace_back(node_id);
    }

    views_by_id.emplace(node_id, std::move(view));
  }

  std::vector<int32_t> node_ids_dfs;
  ExtractNodeIdsDfs(root_node_id, child_ids_by_parent, views_by_id,
                    node_ids_dfs);

  std::vector<ViewDecoder> views_dfs;
  views_dfs.reserve(node_ids_dfs.size());
  for (int32_t id : node_ids_dfs) {
    views_dfs.emplace_back(std::move(views_by_id.at(id)));
  }
  return views_dfs;
}

}  // namespace perfetto::trace_processor::winscope::viewcapture
