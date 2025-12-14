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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_TEST_UTILS_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_TEST_UTILS_H_

#include <optional>
#include <vector>

#include "protos/perfetto/trace/android/surfaceflinger_common.gen.h"
#include "protos/perfetto/trace/android/surfaceflinger_layers.gen.h"
#include "src/trace_processor/importers/proto/winscope/winscope_geometry_test_utils.h"

namespace perfetto::trace_processor::winscope::surfaceflinger_layers::test {

struct Color {
  float r;
  float g;
  float b;
  float a;
};

struct ActiveBuffer {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  int32_t format;
};

namespace {
using LayerProto = protos::gen::LayerProto;
void UpdateColor(protos::gen::LayerProto* layer, Color color) {
  auto* color_proto = layer->mutable_color();
  color_proto->set_r(color.r);
  color_proto->set_g(color.g);
  color_proto->set_b(color.b);
  color_proto->set_a(color.a);
}

void UpdateActiveBuffer(protos::gen::LayerProto* layer, ActiveBuffer buffer) {
  auto* buffer_proto = layer->mutable_active_buffer();
  buffer_proto->set_width(buffer.width);
  buffer_proto->set_height(buffer.height);
  buffer_proto->set_stride(buffer.stride);
  buffer_proto->set_format(buffer.format);
}
}  // namespace

class Layer {
 public:
  explicit Layer() = default;

  Layer& SetColor(Color value) {
    color_ = value;
    return *this;
  }

  Layer& SetActiveBuffer(ActiveBuffer value) {
    active_buffer_ = value;
    return *this;
  }

  Layer& SetFlags(uint32_t value) {
    flags_ = value;
    return *this;
  }

  Layer& SetParent(int32_t value) {
    parent_ = value;
    return *this;
  }

  Layer& SetZOrderRelativeOf(uint32_t value) {
    z_order_relative_of_ = value;
    return *this;
  }

  Layer& SetSourceBounds(geometry::Rect value) {
    source_bounds_ = value;
    return *this;
  }

  Layer& SetScreenBounds(geometry::Rect value) {
    screen_bounds_ = value;
    return *this;
  }

  Layer& SetBounds(geometry::Rect value) {
    bounds_ = value;
    return *this;
  }

  Layer& InitializeVisibleRegion() {
    std::vector<geometry::Rect> rects;
    visible_region_rects_ = rects;
    return *this;
  }

  Layer& AddVisibleRegionRect(geometry::Rect value) {
    if (!visible_region_rects_.has_value()) {
      InitializeVisibleRegion();
    }
    visible_region_rects_->push_back(value);
    return *this;
  }

  Layer& SetIsOpaque(bool value) {
    is_opaque_ = value;
    return *this;
  }

  Layer& SetLayerStack(uint32_t value) {
    layer_stack_ = value;
    return *this;
  }

  Layer& SetZ(int32_t value) {
    z_ = value;
    return *this;
  }

  Layer& SetId(int32_t value) {
    id_ = value;
    return *this;
  }

  Layer& NullifyId() {
    nullify_id_ = true;
    return *this;
  }

  std::optional<Color> color_;
  std::optional<ActiveBuffer> active_buffer_;
  std::optional<uint32_t> flags_;
  std::optional<int32_t> parent_;
  std::optional<int32_t> z_order_relative_of_;
  std::optional<geometry::Rect> source_bounds_;
  std::optional<geometry::Rect> screen_bounds_;
  std::optional<geometry::Rect> bounds_;
  std::optional<std::vector<geometry::Rect>> visible_region_rects_;
  std::optional<bool> is_opaque_;
  std::optional<uint32_t> layer_stack_;
  std::optional<int32_t> z_;
  std::optional<int32_t> id_;
  bool nullify_id_ = false;
};

class SnapshotProtoBuilder {
 public:
  explicit SnapshotProtoBuilder() = default;

  SnapshotProtoBuilder& SetExcludesCompositionState(bool value) {
    excludes_composition_state_ = value;
    return *this;
  }

  SnapshotProtoBuilder& AddLayer(const Layer& value) {
    layers_.push_back(value);
    return *this;
  }

  std::string Build() {
    protos::gen::LayersSnapshotProto snapshot_proto;
    snapshot_proto.set_excludes_composition_state(excludes_composition_state_);
    auto* layers_proto = snapshot_proto.mutable_layers();

    auto i = 1;
    for (const auto& layer : layers_) {
      auto* layer_proto = layers_proto->add_layers();

      if (!layer.nullify_id_) {
        layer_proto->set_id(layer.id_.has_value() ? layer.id_.value() : i);
      }
      i++;

      if (layer.color_.has_value()) {
        UpdateColor(layer_proto, layer.color_.value());
      }
      if (layer.active_buffer_.has_value()) {
        UpdateActiveBuffer(layer_proto, layer.active_buffer_.value());
      }
      if (layer.source_bounds_.has_value()) {
        geometry::test::UpdateRect(layer_proto->mutable_source_bounds(),
                                   layer.source_bounds_.value());
      }
      if (layer.screen_bounds_.has_value()) {
        geometry::test::UpdateRect(layer_proto->mutable_screen_bounds(),
                                   layer.screen_bounds_.value());
      }
      if (layer.bounds_.has_value()) {
        geometry::test::UpdateRect(layer_proto->mutable_bounds(),
                                   layer.bounds_.value());
      }
      if (layer.flags_.has_value()) {
        layer_proto->set_flags(layer.flags_.value());
      }
      if (layer.parent_.has_value()) {
        layer_proto->set_parent(layer.parent_.value());
      }
      if (layer.z_order_relative_of_.has_value()) {
        layer_proto->set_z_order_relative_of(
            layer.z_order_relative_of_.value());
      }
      if (layer.visible_region_rects_.has_value()) {
        auto* visible_region_proto = layer_proto->mutable_visible_region();
        for (auto& rect : layer.visible_region_rects_.value()) {
          geometry::test::UpdateRect(visible_region_proto->add_rect(), rect);
        }
      }
      if (layer.is_opaque_.has_value()) {
        layer_proto->set_is_opaque(layer.is_opaque_.value());
      }
      if (layer.layer_stack_.has_value()) {
        layer_proto->set_layer_stack(layer.layer_stack_.value());
      }
      if (layer.z_.has_value()) {
        layer_proto->set_z(layer.z_.value());
      }
    }

    return snapshot_proto.SerializeAsString();
  }

 private:
  bool excludes_composition_state_ = false;
  std::vector<Layer> layers_;
};

}  // namespace perfetto::trace_processor::winscope::surfaceflinger_layers::test

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_TEST_UTILS_H_
