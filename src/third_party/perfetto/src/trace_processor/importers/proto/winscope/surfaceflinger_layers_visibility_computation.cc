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

#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_visibility_computation.h"
#include <optional>
#include "perfetto/ext/base/string_view.h"
#include "protos/perfetto/trace/android/graphics/rect.pbzero.h"
#include "protos/perfetto/trace/android/surfaceflinger_common.pbzero.h"

namespace perfetto::trace_processor::winscope::surfaceflinger_layers {

// Layer helper methods only used by this computation.

namespace {
using ColorDecoder = protos::pbzero::ColorProto::Decoder;

bool IsHiddenByParent(
    const LayerDecoder& layer,
    const std::unordered_map<int32_t, LayerDecoder>& layers_by_id) {
  if (layer::IsRootLayer(layer)) {
    return false;
  }
  const auto& parent_layer = layers_by_id.find(layer.parent())->second;
  return (layer::IsHiddenByPolicy(parent_layer) ||
          IsHiddenByParent(parent_layer, layers_by_id));
}

bool IsActiveBufferEmpty(const LayerDecoder& layer) {
  if (!layer.has_active_buffer()) {
    return true;
  }
  protos::pbzero::ActiveBufferProto::Decoder buffer(layer.active_buffer());
  return buffer.format() <= 0 && buffer.height() <= 0 && buffer.stride() <= 0 &&
         buffer.width() <= 0;
}

bool HasValidRgb(const ColorDecoder& color) {
  return color.r() >= 0 && color.g() >= 0 && color.b() >= 0;
}

bool HasEffects(const LayerDecoder& layer) {
  if (layer.shadow_radius() > 0) {
    return true;
  }
  if (!layer.has_color()) {
    return false;
  }
  ColorDecoder color(layer.color());
  if (color.a() <= 0) {
    return false;
  }
  return HasValidRgb(color);
}

bool HasZeroAlpha(const LayerDecoder& layer) {
  if (!layer.has_color()) {
    return true;
  }
  auto alpha = ColorDecoder(layer.color()).a();
  return alpha <= 0 && alpha > -1;
}

bool HasEmptyVisibleRegion(const LayerDecoder& layer) {
  if (!layer.has_visible_region()) {
    return true;
  }
  const auto region =
      protos::pbzero::RegionProto::Decoder(layer.visible_region());
  if (region.has_rect()) {
    for (auto it = region.rect(); it; ++it) {
      protos::pbzero::RectProto::Decoder rect(*it);
      if (!geometry::Rect(rect).IsEmpty()) {
        return false;
      }
    }
  }
  return true;
}

bool HasVisibleRegion(const LayerDecoder& layer,
                      bool excludes_composition_state) {
  if (excludes_composition_state) {
    // Doesn't include state sent during composition like visible region and
    // composition type, so we fallback on the bounds as the visible region
    return layer.has_bounds() && !layer::GetBounds(layer).IsEmpty();
  }
  return !HasEmptyVisibleRegion(layer);
}

bool LayerContains(const LayerDecoder& layer,
                   const LayerDecoder& other,
                   const std::optional<geometry::Rect> crop) {
  auto transform_type_layer = 0;
  if (layer.has_transform()) {
    protos::pbzero::TransformProto::Decoder transform(layer.transform());
    transform_type_layer = transform.type();
  }

  auto transform_type_other = 0;
  if (layer.has_transform()) {
    protos::pbzero::TransformProto::Decoder transform(layer.transform());
    transform_type_other = transform.type();
  }
  if (transform::IsInvalidRotation(transform_type_layer) ||
      transform::IsInvalidRotation(transform_type_other)) {
    return false;
  }

  auto layer_bounds = layer::GetCroppedScreenBounds(layer, crop);
  auto other_bounds = layer::GetCroppedScreenBounds(other, crop);

  if (!layer_bounds.has_value() || !other_bounds.has_value()) {
    return false;
  }

  layer_bounds.value().radii = layer::GetCornerRadii(layer);
  other_bounds.value().radii = layer::GetCornerRadii(other);
  return layer_bounds->ContainsRect(other_bounds.value());
}

bool LayerOverlaps(const LayerDecoder& layer,
                   const LayerDecoder& other,
                   const std::optional<geometry::Rect> crop) {
  auto layer_bounds = layer::GetCroppedScreenBounds(layer, crop);
  auto other_bounds = layer::GetCroppedScreenBounds(other, crop);

  return layer_bounds.has_value() && other_bounds.has_value() &&
         layer_bounds->IntersectsRect(other_bounds.value());
}

bool IsOpaque(const LayerDecoder& layer) {
  if (!layer.has_color()) {
    return false;
  }
  ColorDecoder color(layer.color());
  if (color.a() < 1) {
    return false;
  }
  return layer.is_opaque();
}

bool IsColorEmpty(const LayerDecoder& layer) {
  if (!layer.has_color()) {
    return true;
  }
  if (HasZeroAlpha(layer)) {
    return true;
  }
  return !HasValidRgb(ColorDecoder(layer.color()));
}

geometry::Rect GetDisplayCrop(
    const LayerDecoder& layer,
    const protos::pbzero::LayersSnapshotProto::Decoder& snapshot_decoder) {
  geometry::Rect display_crop = geometry::Rect();
  if (!layer.has_layer_stack()) {
    return display_crop;
  }
  auto layer_stack = layer.layer_stack();
  for (auto it = snapshot_decoder.displays(); it; ++it) {
    protos::pbzero::DisplayProto::Decoder display(*it);
    if (!display.has_layer_stack() || display.layer_stack() != layer_stack) {
      continue;
    }
    if (!display.has_layer_stack_space_rect()) {
      continue;
    }
    display_crop = display::MakeLayerStackSpaceRect(display);
  }
  return display_crop;
}
}  // namespace

VisibilityComputation::VisibilityComputation(
    const protos::pbzero::LayersSnapshotProto::Decoder& snapshot_decoder,
    const std::vector<LayerDecoder>& layers_top_to_bottom,
    const std::unordered_map<int32_t, LayerDecoder>& layers_by_id,
    StringPool* pool)
    : snapshot_decoder_(snapshot_decoder),
      layers_top_to_bottom_(layers_top_to_bottom),
      layers_by_id_(layers_by_id),
      pool_(pool),
      flag_is_hidden_id_(pool_->InternString("flag is hidden")),
      buffer_is_empty_id_(pool_->InternString("buffer is empty")),
      alpha_is_zero_id_(pool_->InternString("alpha is 0")),
      bounds_is_zero_id_(pool_->InternString("bounds is 0x0")),
      crop_is_zero_id_(pool_->InternString("crop is 0x0")),
      transform_is_invalid_id_(pool_->InternString("transform is invalid")),
      no_effects_id_(
          pool_->InternString("does not have color fill, shadow or blur")),
      empty_visible_region_id_(pool_->InternString(
          "visible region calculated by Composition Engine is empty")),
      null_visible_region_id_(pool_->InternString("null visible region")),
      occluded_id_(pool_->InternString("occluded")) {}

std::unordered_map<int32_t, VisibilityProperties>
VisibilityComputation::Compute() {
  std::unordered_map<int32_t, VisibilityProperties> computed_visibility;
  auto excludes_composition_state =
      snapshot_decoder_.has_excludes_composition_state()
          ? snapshot_decoder_.excludes_composition_state()
          : true;
  for (auto it = layers_top_to_bottom_.begin();
       it != layers_top_to_bottom_.end(); it++) {
    if (!it->has_id()) {
      continue;
    }
    auto crop = GetDisplayCrop(*it, snapshot_decoder_);
    const auto& res = IsLayerVisible(*it, excludes_composition_state, crop);

    computed_visibility[it->id()] = res;
  }
  return computed_visibility;
}

VisibilityProperties VisibilityComputation::IsLayerVisible(
    const LayerDecoder& layer,
    bool excludes_composition_state,
    const std::optional<geometry::Rect> crop) {
  VisibilityProperties res;
  res.is_visible = IsLayerVisibleInIsolation(layer, excludes_composition_state);

  if (res.is_visible) {
    for (const auto opaque_layer_id : opaque_layer_ids) {
      const auto& opaque_layer = layers_by_id_.at(opaque_layer_id);
      if (opaque_layer.has_layer_stack() != layer.has_layer_stack()) {
        continue;
      }
      if (opaque_layer.layer_stack() != layer.layer_stack()) {
        continue;
      }

      if (LayerContains(opaque_layer, layer, crop)) {
        res.is_visible = false;
        res.occluding_layers.push_back(opaque_layer.id());
        continue;
      }

      if (!LayerOverlaps(opaque_layer, layer, crop)) {
        continue;
      }

      if (std::find(res.occluding_layers.begin(), res.occluding_layers.end(),
                    opaque_layer.id()) == res.occluding_layers.end()) {
        res.partially_occluding_layers.push_back(opaque_layer.id());
      }
    }

    for (const auto translucent_layer_id : translucent_layer_ids) {
      const auto& translucent_layer = layers_by_id_.at(translucent_layer_id);
      if (translucent_layer.has_layer_stack() != layer.has_layer_stack()) {
        continue;
      }
      if (translucent_layer.layer_stack() != layer.layer_stack()) {
        continue;
      }
      if (LayerOverlaps(translucent_layer, layer, crop)) {
        res.covering_layers.push_back(translucent_layer.id());
      }
    }

    if (IsOpaque(layer)) {
      opaque_layer_ids.push_back(layer.id());
    } else {
      translucent_layer_ids.push_back(layer.id());
    }
  }

  if (!res.is_visible) {
    res.visibility_reasons = GetVisibilityReasons(
        layer, excludes_composition_state, res.occluding_layers);
  }

  return res;
}

bool VisibilityComputation::IsLayerVisibleInIsolation(
    const LayerDecoder& layer,
    bool excludes_composition_state) {
  if (IsHiddenByParent(layer, layers_by_id_) ||
      layer::IsHiddenByPolicy(layer)) {
    return false;
  }
  if (!layer.has_color()) {
    return false;
  }
  ColorDecoder color(layer.color());
  if (color.a() <= 0) {
    return false;
  }
  if (IsActiveBufferEmpty(layer) && !HasEffects(layer)) {
    return false;
  }
  return HasVisibleRegion(layer, excludes_composition_state);
}

std::vector<StringPool::Id> VisibilityComputation::GetVisibilityReasons(
    const LayerDecoder& layer,
    bool excludes_composition_state,
    const std::vector<int32_t>& occluding_layers) {
  std::vector<StringPool::Id> reasons;

  if (layer::IsHiddenByPolicy(layer)) {
    reasons.push_back(flag_is_hidden_id_);
  }

  if (IsHiddenByParent(layer, layers_by_id_)) {
    reasons.push_back(pool_->InternString(base::StringView(
        "hidden by parent " + std::to_string(layer.parent()))));
  }

  if (IsActiveBufferEmpty(layer)) {
    reasons.push_back(buffer_is_empty_id_);
  }

  if (HasZeroAlpha(layer)) {
    reasons.push_back(alpha_is_zero_id_);
  }

  if (!layer.has_bounds() || layer::GetBounds(layer).IsEmpty()) {
    reasons.push_back(bounds_is_zero_id_);

    if (!layer.has_color() || (layer.has_color() && IsColorEmpty(layer))) {
      reasons.push_back(crop_is_zero_id_);
    }
  }

  if (!layer::GetTransformMatrix(layer).IsValid()) {
    reasons.push_back(transform_is_invalid_id_);
  }

  if (IsActiveBufferEmpty(layer) && !HasEffects(layer) &&
      !(layer.background_blur_radius() > 0)) {
    reasons.push_back(no_effects_id_);
  }

  if (layer.has_visible_region() && HasEmptyVisibleRegion(layer)) {
    reasons.push_back(empty_visible_region_id_);
  }

  if (!layer.has_visible_region() && !excludes_composition_state) {
    reasons.push_back(null_visible_region_id_);
  }

  if (occluding_layers.size() > 0) {
    reasons.push_back(occluded_id_);
  }

  return reasons;
}
}  // namespace perfetto::trace_processor::winscope::surfaceflinger_layers
