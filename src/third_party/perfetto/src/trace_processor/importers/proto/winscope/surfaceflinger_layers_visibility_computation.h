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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_VISIBILITY_COMPUTATION_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_VISIBILITY_COMPUTATION_H_

#include <unordered_map>
#include <unordered_set>
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/android/surfaceflinger_layers.pbzero.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_utils.h"
#include "src/trace_processor/importers/proto/winscope/winscope_geometry.h"

namespace perfetto::trace_processor::winscope::surfaceflinger_layers {

namespace {
using LayerDecoder = protos::pbzero::LayerProto::Decoder;
}

// Computes visibility properties for every layer in hierarchy, based on its
// properties and its position in the drawing order. In addition to computing
// its visibility, we store a fixed set of reasons if it is not visible, and the
// ids of any layers that are occluding, partially occluding or covering it.

struct VisibilityProperties {
  bool is_visible;
  std::vector<StringPool::Id> visibility_reasons;
  std::vector<int32_t> occluding_layers;
  std::vector<int32_t> partially_occluding_layers;
  std::vector<int32_t> covering_layers;
};

class VisibilityComputation {
 public:
  explicit VisibilityComputation(
      const protos::pbzero::LayersSnapshotProto::Decoder& snapshot_decoder,
      const std::vector<LayerDecoder>& layers_top_to_bottom,
      const std::unordered_map<int32_t, LayerDecoder>& layers_by_id,
      StringPool* pool);

  std::unordered_map<int32_t, VisibilityProperties> Compute();

 private:
  const protos::pbzero::LayersSnapshotProto::Decoder& snapshot_decoder_;
  const std::vector<LayerDecoder>& layers_top_to_bottom_;
  const std::unordered_map<int32_t, LayerDecoder>& layers_by_id_;

  StringPool* pool_;
  StringPool::Id flag_is_hidden_id_;
  StringPool::Id buffer_is_empty_id_;
  StringPool::Id alpha_is_zero_id_;
  StringPool::Id bounds_is_zero_id_;
  StringPool::Id crop_is_zero_id_;
  StringPool::Id transform_is_invalid_id_;
  StringPool::Id no_effects_id_;
  StringPool::Id empty_visible_region_id_;
  StringPool::Id null_visible_region_id_;
  StringPool::Id occluded_id_;

  std::vector<int32_t> opaque_layer_ids = {};
  std::vector<int32_t> translucent_layer_ids = {};

  VisibilityProperties IsLayerVisible(const LayerDecoder& layer,
                                      bool excludes_composition_state,
                                      std::optional<geometry::Rect> crop);

  // Return true if layer is visible due to its properties. Visibility may
  // change based on the hierarchy drawing order, if a layer is occluded by
  // another.
  bool IsLayerVisibleInIsolation(const LayerDecoder& layer,
                                 bool excludes_composition_state);

  // Return list of reasons why a layer is not visible. These are added to the
  // args table.
  std::vector<StringPool::Id> GetVisibilityReasons(
      const LayerDecoder& layer,
      bool excludes_composition_state,
      const std::vector<int32_t>& occluding_layers);
};

}  // namespace perfetto::trace_processor::winscope::surfaceflinger_layers

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_VISIBILITY_COMPUTATION_H_
