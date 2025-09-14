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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_RECT_COMPUTATION_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_RECT_COMPUTATION_H_

#include <optional>
#include <unordered_map>
#include <vector>
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/android/surfaceflinger_common.pbzero.h"
#include "protos/perfetto/trace/android/surfaceflinger_layers.pbzero.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_visibility_computation.h"
#include "src/trace_processor/importers/proto/winscope/winscope_geometry.h"
#include "src/trace_processor/importers/proto/winscope/winscope_rect_tracker.h"
#include "src/trace_processor/importers/proto/winscope/winscope_transform_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/winscope_tables_py.h"

namespace perfetto::trace_processor::winscope::surfaceflinger_layers {

namespace {
using TraceRectTableId = tables::WinscopeTraceRectTable::Id;
using SnapshotDecoder = protos::pbzero::LayersSnapshotProto::Decoder;
using LayerDecoder = protos::pbzero::LayerProto::Decoder;
}  // namespace

struct SurfaceFlingerRects {
  std::optional<TraceRectTableId> layer_rect = std::nullopt;
  std::optional<TraceRectTableId> input_rect = std::nullopt;
};

class RectComputation {
 public:
  explicit RectComputation(
      const protos::pbzero::LayersSnapshotProto::Decoder& snapshot_decoder,
      const std::vector<LayerDecoder>& layers_top_to_bottom,
      const std::unordered_map<int32_t, VisibilityProperties>&
          computed_visibility,
      const std::unordered_map<uint32_t, geometry::Rect>&
          displays_by_layer_stack,
      WinscopeRectTracker& rect_tracker,
      WinscopeTransformTracker& transform_tracker);

  const std::unordered_map<int32_t, SurfaceFlingerRects> Compute();

 private:
  const SnapshotDecoder& snapshot_decoder_;
  const std::vector<LayerDecoder>& layers_top_to_bottom_;
  const std::unordered_map<int32_t, VisibilityProperties>& computed_visibility_;
  const std::unordered_map<uint32_t, geometry::Rect>& displays_by_layer_stack_;
  WinscopeRectTracker& rect_tracker_;
  WinscopeTransformTracker& transform_tracker_;

  const geometry::Rect DEFAULT_INVALID_BOUNDS =
      geometry::Rect(-50000, -50000, 50000, 50000);

  std::optional<TraceRectTableId> TryInsertBoundsRect(
      const LayerDecoder& layer,
      std::vector<geometry::Rect>& invalid_bounds,
      std::unordered_map<int32_t, int>& current_z_by_layer_stack);

  std::optional<TraceRectTableId> TryInsertInputRect(
      const LayerDecoder& layer,
      std::vector<geometry::Rect>& invalid_bounds,
      std::unordered_map<int32_t, int>& current_z_by_layer_stack,
      std::unordered_map<uint32_t, geometry::TransformMatrix>&
          display_transforms);

  TraceRectTableId InsertLayerTraceRectRow(const LayerDecoder& layer_decoder,
                                           bool is_computed_visible,
                                           int absolute_z);

  TraceRectTableId InsertInputTraceRectRow(geometry::Rect& frame_rect,
                                           geometry::TransformMatrix& matrix,
                                           int absolute_z,
                                           int layer_stack,
                                           bool is_visible,
                                           bool is_spy);
};
}  // namespace perfetto::trace_processor::winscope::surfaceflinger_layers

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_RECT_COMPUTATION_H_
