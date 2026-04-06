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
#include <utility>
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_utils.h"

namespace perfetto::trace_processor::winscope::surfaceflinger_layers {

namespace {

// Sorts in ascending order. When z-order is the same, we sort such that the
// layer with the greater layer id is drawn on top.
void SortByZThenLayerId(
    std::vector<int32_t>& layer_ids,
    const std::unordered_map<int32_t, LayerDecoder>& layers_by_id) {
  std::sort(layer_ids.begin(), layer_ids.end(),
            [&layers_by_id](int32_t a, int32_t b) {
              const auto& layer_a = layers_by_id.at(a);
              const auto& layer_b = layers_by_id.at(b);
              return std::make_tuple(layer_a.z(), layer_a.id()) <
                     std::make_tuple(layer_b.z(), layer_b.id());
            });
}

// Extract layers bottom-to-top according to layer drawing order from
// /frameworks/native/services/surfaceflinger/FrontEnd/readme.md.
void ExtractBottomToTop(
    int32_t node_id,
    std::unordered_map<int32_t, std::vector<int32_t>>& child_ids_by_z_parent,
    std::unordered_map<int32_t, LayerDecoder>& layers_by_id,
    std::vector<int32_t>& layer_ids_bottom_to_top) {
  std::vector<int32_t> child_ids;
  auto pos = child_ids_by_z_parent.find(node_id);
  if (pos != child_ids_by_z_parent.end()) {
    child_ids = pos->second;
    SortByZThenLayerId(child_ids, layers_by_id);
  }

  for (auto it = child_ids.begin(); it != child_ids.end(); ++it) {
    const LayerDecoder& child_layer = layers_by_id.at(*it);
    if (child_layer.z() < 0) {
      ExtractBottomToTop(*it, child_ids_by_z_parent, layers_by_id,
                         layer_ids_bottom_to_top);
    }
  }

  layer_ids_bottom_to_top.emplace_back(node_id);

  for (auto it = child_ids.begin(); it != child_ids.end(); ++it) {
    const LayerDecoder& child_layer = layers_by_id.at(*it);
    if (child_layer.z() >= 0) {
      ExtractBottomToTop(*it, child_ids_by_z_parent, layers_by_id,
                         layer_ids_bottom_to_top);
    }
  }
}

// We work with layer ids to enable sorting and copying, as LayerDecoder can
// only be moved.
std::vector<LayerDecoder> ExtractLayersByZOrder(
    std::vector<int32_t>& root_layer_ids,
    std::unordered_map<int32_t, std::vector<int32_t>>& child_ids_by_z_parent,
    std::unordered_map<int32_t, LayerDecoder>& layers_by_id) {
  SortByZThenLayerId(root_layer_ids, layers_by_id);

  std::vector<int32_t> layer_ids_bottom_to_top;

  for (auto it = root_layer_ids.begin(); it != root_layer_ids.end(); it++) {
    ExtractBottomToTop(*it, child_ids_by_z_parent, layers_by_id,
                       layer_ids_bottom_to_top);
  }

  std::reverse(layer_ids_bottom_to_top.begin(), layer_ids_bottom_to_top.end());
  std::vector<LayerDecoder> layers_top_to_bottom;
  for (int32_t id : layer_ids_bottom_to_top) {
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
