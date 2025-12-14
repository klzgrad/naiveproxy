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

#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_rect_computation.h"
#include <algorithm>
#include <optional>
#include <unordered_map>
#include "protos/perfetto/trace/android/graphics/rect.pbzero.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_utils.h"

namespace perfetto::trace_processor::winscope::surfaceflinger_layers {

// Layer helper methods used only by this computation.

namespace {

// InputConfig constants defined in the platform:
//   frameworks/native/libs/input/android/os/InputConfig.aidl
enum InputConfig {
  NOT_TOUCHABLE = 1 << 3,
  IS_WALLPAPER = 1 << 6,
  SPY = 1 << 14,
};

// Invalid bounds are set in platform as 10 times the max dimensions found
// in all current displays.
std::vector<geometry::Rect> MakeInvalidBoundsFromSize(
    const geometry::Size& size) {
  const auto x = size.w * 10;
  const auto y = size.h * 10;
  const auto bounds = geometry::Rect(-x, -y, x, y);
  const auto rotated_bounds = geometry::Rect(
      bounds.y, bounds.x, bounds.y + bounds.h, bounds.x + bounds.w);
  return {bounds, rotated_bounds};
}

// The max dimensions from all current displays differ based on when a layer
// was created, so we construct possible invalid bounds from all max-dimension
// combinations for a particular SF frame.
std::vector<geometry::Rect> MakeInvalidBoundsFromDisplays(
    const SnapshotDecoder& snapshot_decoder) {
  std::vector<geometry::Rect> invalid_bounds;
  if (!snapshot_decoder.has_displays()) {
    return invalid_bounds;
  }
  auto max_size = geometry::Size{0, 0};
  for (auto it = snapshot_decoder.displays(); it; ++it) {
    protos::pbzero::DisplayProto::Decoder display(*it);
    auto display_size = display::GetDisplaySize(display);
    for (const auto& rect : MakeInvalidBoundsFromSize(display_size)) {
      invalid_bounds.push_back(rect);
    }
    max_size.w = std::max(max_size.w, display_size.w);
    max_size.h = std::max(max_size.h, display_size.h);
  }
  for (const auto& rect : MakeInvalidBoundsFromSize(max_size)) {
    invalid_bounds.push_back(rect);
  }
  return invalid_bounds;
}

// Display transforms must be reconstructed based on type and layer stack space
// rect.
std::unordered_map<uint32_t, geometry::TransformMatrix>
ExtractDisplayTransforms(const SnapshotDecoder& snapshot_decoder) {
  std::unordered_map<uint32_t, geometry::TransformMatrix> transforms;

  for (auto it = snapshot_decoder.displays(); it; ++it) {
    protos::pbzero::DisplayProto::Decoder display(*it);
    auto matrix = display.has_layer_stack()
                      ? display::GetTransformMatrix(display)
                      : geometry::TransformMatrix{};

    protos::pbzero::TransformProto::Decoder transform(display.transform());

    if (transform.has_type() && display.has_layer_stack_space_rect()) {
      const auto& layer_stack_space_rect =
          display::MakeLayerStackSpaceRect(display);
      auto transform_type = transform.type();

      if (transform::IsRotated180(transform_type)) {
        matrix.tx = layer_stack_space_rect.w;
        matrix.ty = layer_stack_space_rect.h;
      } else if (transform::IsRotated270(transform_type)) {
        matrix.tx = layer_stack_space_rect.w;
      } else if (transform::IsRotated90(transform_type)) {
        matrix.ty = layer_stack_space_rect.h;
      }
    }
    transforms[display.layer_stack()] = matrix;
  }

  return transforms;
}

// Layer opacity should only be set for rects that are visible, or which
// have an alpha value.
std::optional<double> GetLayerOpacity(const LayerDecoder& layer_decoder,
                                      bool is_computed_visible) {
  if (layer_decoder.has_color()) {
    protos::pbzero::ColorProto::Decoder color(layer_decoder.color());
    if (color.has_a()) {
      return color.a();
    }
  }
  if (is_computed_visible) {
    return 0;
  }
  return std::nullopt;
}

// Input window frames are in the layer space - we transform them to the
// display space for comparison with other rects.
geometry::Rect MakeFrameRect(
    const protos::pbzero::InputWindowInfoProto::Decoder& input_window_info,
    std::optional<geometry::TransformMatrix>& display_transform,
    geometry::TransformMatrix& inverse_layer_transform) {
  if (!input_window_info.has_frame()) {
    return geometry::Rect();
  }
  protos::pbzero::RectProto::Decoder frame(input_window_info.frame());
  auto frame_rect = geometry::Rect(frame);
  if (display_transform.has_value()) {
    frame_rect = display_transform->TransformRect(frame_rect);
  }
  return inverse_layer_transform.TransformRect(frame_rect);
}

// Input windows may be shaded in Winscope based on their touchable region.
std::optional<geometry::Region> TryMakeFillRegion(
    uint32_t input_config,
    const protos::pbzero::InputWindowInfoProto::Decoder& input_window_info,
    std::optional<geometry::TransformMatrix>& display_transform,
    geometry::TransformMatrix& inverse_layer_transform,
    const std::optional<geometry::Rect>& display) {
  std::optional<geometry::Region> fill_region;

  const bool is_touchable = (input_config & InputConfig::NOT_TOUCHABLE) == 0;
  if (!is_touchable) {
    fill_region = geometry::Region{};
  } else if (input_window_info.has_touchable_region()) {
    fill_region = geometry::Region{};

    protos::pbzero::RegionProto::Decoder region(
        input_window_info.touchable_region());
    for (auto it = region.rect(); it; ++it) {
      protos::pbzero::RectProto::Decoder rect(*it);
      fill_region->rects.push_back(geometry::Rect(rect));
    }

    if (display_transform.has_value()) {
      for (auto& rect : fill_region->rects) {
        rect = display_transform->TransformRect(rect);
      }
    }
    for (auto& rect : fill_region->rects) {
      rect = inverse_layer_transform.TransformRect(rect);
    }
    if (display.has_value()) {
      for (auto& rect : fill_region->rects) {
        rect = rect.CropRect(display.value());
      }
    }
  }

  if (fill_region.has_value() && fill_region->rects.size() == 0) {
    fill_region->rects.push_back(geometry::Rect());
  }
  return fill_region;
}

}  // namespace

RectComputation::RectComputation(
    const protos::pbzero::LayersSnapshotProto::Decoder& snapshot_decoder,
    const std::vector<LayerDecoder>& layers_top_to_bottom,
    const std::unordered_map<int32_t, VisibilityProperties>&
        computed_visibility,
    const std::unordered_map<uint32_t, geometry::Rect>& displays_by_layer_stack,
    WinscopeRectTracker& rect_tracker,
    WinscopeTransformTracker& transform_tracker)
    : snapshot_decoder_(snapshot_decoder),
      layers_top_to_bottom_(layers_top_to_bottom),
      computed_visibility_(computed_visibility),
      displays_by_layer_stack_(displays_by_layer_stack),
      rect_tracker_(rect_tracker),
      transform_tracker_(transform_tracker) {}

const std::unordered_map<int32_t, SurfaceFlingerRects>
RectComputation::Compute() {
  std::unordered_map<int32_t, int> current_z_by_layer_stack_bounds;
  std::unordered_map<int32_t, int> current_z_by_layer_stack_input;
  for (const auto& it : displays_by_layer_stack_) {
    current_z_by_layer_stack_bounds[static_cast<int32_t>(it.first)] = 1;
    current_z_by_layer_stack_input[static_cast<int32_t>(it.first)] = 1;
  }

  auto invalid_bounds = MakeInvalidBoundsFromDisplays(snapshot_decoder_);
  std::unordered_map<uint32_t, geometry::TransformMatrix> display_transforms =
      ExtractDisplayTransforms(snapshot_decoder_);

  std::unordered_map<int32_t, SurfaceFlingerRects> rects;
  // iterate from bottom to top (i.e. drawing order) to correctly increment z
  for (auto it = layers_top_to_bottom_.rbegin();
       it != layers_top_to_bottom_.rend(); ++it) {
    const auto& layer = *it;
    const auto& bounds_rect_id = TryInsertBoundsRect(
        layer, invalid_bounds, current_z_by_layer_stack_bounds);
    const auto& input_rect_id =
        TryInsertInputRect(layer, invalid_bounds,
                           current_z_by_layer_stack_input, display_transforms);
    rects[layer.id()] = {bounds_rect_id, input_rect_id};
  }
  return rects;
}

std::optional<TraceRectTableId> RectComputation::TryInsertBoundsRect(
    const LayerDecoder& layer,
    std::vector<geometry::Rect>& invalid_bounds,
    std::unordered_map<int32_t, int>& current_z_by_layer_stack) {
  if (!layer.has_id()) {
    return std::nullopt;
  }

  auto screen_bounds_rect = layer::GetCroppedScreenBounds(layer, std::nullopt);
  if (!screen_bounds_rect.has_value()) {
    return std::nullopt;
  }

  int32_t layer_stack =
      layer.has_layer_stack() ? static_cast<int32_t>(layer.layer_stack()) : -1;
  auto absolute_z = current_z_by_layer_stack[layer_stack];
  std::optional<TraceRectTableId> rect_id = std::nullopt;

  auto is_computed_visible =
      computed_visibility_.find(layer.id())->second.is_visible;
  if (is_computed_visible) {
    rect_id = InsertLayerTraceRectRow(layer, is_computed_visible, absolute_z);
  } else {
    const bool invalid_from_displays =
        invalid_bounds.size() > 0 &&
        std::find_if(invalid_bounds.begin(), invalid_bounds.end(),
                     [&](const geometry::Rect& rect) {
                       return screen_bounds_rect->IsAlmostEqual(rect);
                     }) != invalid_bounds.end();

    const bool invalid_screen_bounds =
        screen_bounds_rect->IsAlmostEqual(DEFAULT_INVALID_BOUNDS);

    if (!invalid_from_displays && !invalid_screen_bounds) {
      rect_id = InsertLayerTraceRectRow(layer, is_computed_visible, absolute_z);
    }
  }
  if (rect_id.has_value()) {
    current_z_by_layer_stack[layer_stack] = absolute_z + 1;
  }
  return rect_id;
}

std::optional<TraceRectTableId> RectComputation::TryInsertInputRect(
    const LayerDecoder& layer,
    std::vector<geometry::Rect>& invalid_bounds,
    std::unordered_map<int32_t, int>& current_z_by_layer_stack,
    std::unordered_map<uint32_t, geometry::TransformMatrix>&
        display_transforms) {
  if (!layer.has_id() || !layer.has_input_window_info()) {
    return std::nullopt;
  }

  protos::pbzero::InputWindowInfoProto::Decoder input_window_info(
      layer.input_window_info());
  int32_t layer_stack =
      layer.has_layer_stack() ? static_cast<int32_t>(layer.layer_stack()) : -1;
  auto absolute_z = current_z_by_layer_stack[layer_stack];

  auto layer_transform = layer::GetTransformMatrix(layer);
  auto inverse_layer_transform = layer_transform.Inverse();
  std::optional<geometry::TransformMatrix> display_transform;
  if (layer.has_layer_stack()) {
    auto pos = display_transforms.find(layer.layer_stack());
    if (pos != display_transforms.end()) {
      display_transform = pos->second;
    }
  }

  auto frame_rect = MakeFrameRect(input_window_info, display_transform,
                                  inverse_layer_transform);

  auto input_config = input_window_info.input_config();

  bool should_crop_to_display = false;
  std::optional<geometry::Rect> display;

  if (layer.has_layer_stack()) {
    auto display_pos = displays_by_layer_stack_.find(layer.layer_stack());
    if (display_pos != displays_by_layer_stack_.end()) {
      display = display_pos->second;
      should_crop_to_display =
          frame_rect.IsEmpty() ||
          (input_config & InputConfig::IS_WALLPAPER) != 0 ||
          std::find_if(invalid_bounds.begin(), invalid_bounds.end(),
                       [&](const geometry::Rect& bounds) {
                         return frame_rect.IsAlmostEqual(bounds);
                       }) != invalid_bounds.end();
      if (should_crop_to_display) {
        frame_rect = frame_rect.CropRect(display.value());
      }
    }
  }

  const bool is_visible =
      input_window_info.has_visible()
          ? input_window_info.visible()
          : computed_visibility_.find(layer.id())->second.is_visible;
  const bool is_spy = (input_config & InputConfig::SPY) != 0;

  TraceRectTableId input_rect_id = InsertInputTraceRectRow(
      frame_rect, layer_transform, absolute_z, layer_stack, is_visible, is_spy);

  const auto& fill_region = TryMakeFillRegion(
      input_config, input_window_info, display_transform,
      inverse_layer_transform, should_crop_to_display ? display : std::nullopt);
  if (fill_region.has_value()) {
    for (auto rect : fill_region->rects) {
      tables::WinscopeFillRegionTable::Row row;
      row.rect_id = rect_tracker_.GetOrInsertRow(rect);
      row.trace_rect_id = input_rect_id;
      rect_tracker_.context_->storage->mutable_winscope_fill_region_table()
          ->Insert(row);
    }
  }

  current_z_by_layer_stack[layer_stack] = absolute_z + 1;
  return input_rect_id;
}

TraceRectTableId RectComputation::InsertLayerTraceRectRow(
    const LayerDecoder& layer_decoder,
    bool is_computed_visible,
    int absolute_z) {
  std::optional<double> opacity =
      GetLayerOpacity(layer_decoder, is_computed_visible);

  auto matrix = layer::GetTransformMatrix(layer_decoder);

  tables::WinscopeTraceRectTable::Row row;
  auto bounds_rect = layer::GetBounds(layer_decoder);
  row.rect_id = rect_tracker_.GetOrInsertRow(bounds_rect);
  row.group_id = layer_decoder.has_layer_stack() ? layer_decoder.layer_stack()
                                                 : static_cast<uint32_t>(-1);
  row.depth = static_cast<uint32_t>(absolute_z);
  row.is_visible = is_computed_visible;
  if (opacity != std::nullopt) {
    row.opacity = opacity;
  }

  row.transform_id = transform_tracker_.GetOrInsertRow(matrix);
  row.is_spy = false;
  return rect_tracker_.context_->storage->mutable_winscope_trace_rect_table()
      ->Insert(row)
      .id;
}

TraceRectTableId RectComputation::InsertInputTraceRectRow(
    geometry::Rect& frame_rect,
    geometry::TransformMatrix& matrix,
    int absolute_z,
    int layer_stack,
    bool is_visible,
    bool is_spy) {
  tables::WinscopeTraceRectTable::Row row;
  row.rect_id = rect_tracker_.GetOrInsertRow(frame_rect);
  row.depth = static_cast<uint32_t>(absolute_z);
  row.group_id = static_cast<uint32_t>(layer_stack);
  row.transform_id = transform_tracker_.GetOrInsertRow(matrix);
  row.is_spy = is_spy;
  row.is_visible = is_visible;
  return rect_tracker_.context_->storage->mutable_winscope_trace_rect_table()
      ->Insert(row)
      .id;
}

}  // namespace perfetto::trace_processor::winscope::surfaceflinger_layers
