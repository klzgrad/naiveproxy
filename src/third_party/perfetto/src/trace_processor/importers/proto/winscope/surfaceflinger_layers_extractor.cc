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

#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_extractor.h"

#include <algorithm>
#include <functional>
#include <utility>
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_utils.h"

namespace perfetto::trace_processor::winscope::surfaceflinger_layers {

namespace {
enum ProcessingStage { VisitChildren, Add };

// When z-order is the same, we sort such that the layer with the layer id
// is drawn on top.
void SortByZThenLayerId(
    std::vector<int32_t>& layer_ids,
    const std::unordered_map<int32_t, LayerDecoder>& layers_by_id) {
  std::sort(layer_ids.begin(), layer_ids.end(),
            [&layers_by_id](int32_t a, int32_t b) {
              const auto& layer_a = layers_by_id.at(a);
              const auto& layer_b = layers_by_id.at(b);
              return std::make_tuple(layer_a.z(), layer_a.id()) >
                     std::make_tuple(layer_b.z(), layer_b.id());
            });
}

// We work with layer ids to enable sorting and copying, as LayerDecoder can
// only be moved.
std::vector<LayerDecoder> ExtractLayersByZOrder(
    std::vector<int32_t>& root_layer_ids,
    std::unordered_map<int32_t, std::vector<int32_t>>& child_ids_by_z_parent,
    std::unordered_map<int32_t, LayerDecoder>& layers_by_id) {
  SortByZThenLayerId(root_layer_ids, layers_by_id);

  std::vector<int32_t> layer_ids_top_to_bottom;

  std::vector<std::pair<int32_t, ProcessingStage>> processing_queue;
  for (auto it = root_layer_ids.rbegin(); it != root_layer_ids.rend(); ++it) {
    processing_queue.emplace_back(*it, ProcessingStage::VisitChildren);
  }

  while (!processing_queue.empty()) {
    const auto curr = processing_queue.back();
    processing_queue.pop_back();

    int32_t curr_layer_id = curr.first;
    const LayerDecoder& curr_layer = layers_by_id.at(curr_layer_id);

    std::vector<int32_t> curr_child_ids;
    auto pos = child_ids_by_z_parent.find(curr_layer_id);
    if (pos != child_ids_by_z_parent.end()) {
      curr_child_ids = pos->second;
      SortByZThenLayerId(curr_child_ids, layers_by_id);
    }

    int32_t current_z = curr_layer.z();

    if (curr.second == ProcessingStage::VisitChildren) {
      processing_queue.emplace_back(curr_layer_id, ProcessingStage::Add);

      for (auto it = curr_child_ids.rbegin(); it != curr_child_ids.rend();
           ++it) {
        const LayerDecoder& child_layer = layers_by_id.at(*it);
        if (child_layer.z() >= current_z) {
          processing_queue.emplace_back(*it, ProcessingStage::VisitChildren);
        }
      }
    } else {
      layer_ids_top_to_bottom.emplace_back(curr_layer_id);

      for (auto it = curr_child_ids.rbegin(); it != curr_child_ids.rend();
           ++it) {
        const LayerDecoder& child_layer = layers_by_id.at(*it);
        if (child_layer.z() < current_z) {
          processing_queue.emplace_back(*it, ProcessingStage::VisitChildren);
        }
      }
    }
  }

  std::vector<LayerDecoder> layers_top_to_bottom;
  layers_top_to_bottom.reserve(layer_ids_top_to_bottom.size());
  for (int32_t id : layer_ids_top_to_bottom) {
    layers_top_to_bottom.emplace_back(std::move(layers_by_id.at(id)));
  }
  return layers_top_to_bottom;
}
}  // namespace

// Returns map of layer id to layer, so we can quickly retrieve a layer by its
// id during visibility computation.
std::unordered_map<int32_t, LayerDecoder> ExtractLayersById(
    const LayersDecoder& layers_decoder) {
  std::unordered_map<int32_t, LayerDecoder> layers_by_id;
  for (auto it = layers_decoder.layers(); it; ++it) {
    LayerDecoder layer(*it);
    if (!layer.has_id()) {
      continue;
    }
    layers_by_id.emplace(layer.id(), std::move(layer));
  }
  return layers_by_id;
}

// Returns a vector of layers in top-to-bottom drawing order (z order), so
// we can determine occlusion states during visibility computation and depth
// in rect computation.
std::vector<LayerDecoder> ExtractLayersTopToBottom(
    const LayersDecoder& layers_decoder) {
  std::vector<int32_t> root_layer_ids;
  std::unordered_map<int32_t, std::vector<int32_t>> child_ids_by_z_parent;
  std::unordered_map<int32_t, LayerDecoder> layers_by_id;

  for (auto it = layers_decoder.layers(); it; ++it) {
    LayerDecoder layer(*it);
    if (!layer.has_id()) {
      continue;
    }
    auto layer_id = layer.id();

    if (layer::IsRootLayer(layer) && layer.z_order_relative_of() <= 0) {
      root_layer_ids.emplace_back(layer_id);
    } else {
      const auto parent = layer.parent();
      const auto z_parent = layer.z_order_relative_of();
      if (z_parent > 0) {
        child_ids_by_z_parent[z_parent].emplace_back(layer_id);
      } else if (parent > 0) {
        child_ids_by_z_parent[parent].emplace_back(layer_id);
      }
    }

    layers_by_id.emplace(layer_id, std::move(layer));
  }

  return ExtractLayersByZOrder(root_layer_ids, child_ids_by_z_parent,
                               layers_by_id);
}

}  // namespace perfetto::trace_processor::winscope::surfaceflinger_layers
