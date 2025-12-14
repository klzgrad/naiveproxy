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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_UTILS_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_UTILS_H_

#include <optional>
#include "protos/perfetto/trace/android/graphics/rect.pbzero.h"
#include "protos/perfetto/trace/android/surfaceflinger_common.pbzero.h"
#include "protos/perfetto/trace/android/surfaceflinger_layers.pbzero.h"
#include "src/trace_processor/importers/proto/winscope/winscope_geometry.h"

// Used to manipulate SurfaceFlinger layer data to perform various computations
// during parsing, such as visibility and rects.

namespace perfetto::trace_processor::winscope::surfaceflinger_layers {

namespace {
using TransformDecoder = protos::pbzero::TransformProto::Decoder;
using TransformMatrix = geometry::TransformMatrix;
using Rect = geometry::Rect;
}  // namespace

namespace transform {
namespace {
enum TransformFlag {
  EMPTY = 0x0,
  TRANSLATE_VAL = 0x0001,
  ROTATE_VAL = 0x0002,
  SCALE_VAL = 0x0004,
  FLIP_H_VAL = 0x0100,
  FLIP_V_VAL = 0x0200,
  ROT_90_VAL = 0x0400,
  ROT_INVALID_VAL = 0x8000,
};

bool IsFlagSet(int type, int flag) {
  return (type & flag) == flag;
}

bool IsFlagClear(int type, int flag) {
  return (type & flag) == 0;
}

TransformMatrix ApplyPosToIdentityMatrix(double x, double y) {
  TransformMatrix matrix;
  matrix.tx = x;
  matrix.ty = y;
  return matrix;
}
}  // namespace

inline bool IsInvalidRotation(int type) {
  return IsFlagSet(type, TransformFlag::ROT_INVALID_VAL);
}

// ROT_270 = ROT_90|FLIP_H|FLIP_V
inline bool IsRotated270(int flags) {
  return IsFlagSet(flags, TransformFlag::ROT_90_VAL |
                              TransformFlag::FLIP_V_VAL |
                              TransformFlag::FLIP_H_VAL);
}

// ROT_180 = FLIP_H|FLIP_V
inline bool IsRotated180(int flags) {
  return IsFlagSet(flags,
                   TransformFlag::FLIP_V_VAL | TransformFlag::FLIP_H_VAL);
}

// ROT_90
inline bool IsRotated90(int flags) {
  return IsFlagSet(flags, TransformFlag::ROT_90_VAL);
}

// Returns true if the transform a valid rotation or translation.
inline bool IsSimpleTransform(int type) {
  return IsFlagClear(type,
                     TransformFlag::ROT_INVALID_VAL | TransformFlag::SCALE_VAL);
}

// Reconstructs a transform matrix from type and position in proto data.
inline TransformMatrix GetTransformMatrix(int type, double x, double y) {
  if (!type) {
    return ApplyPosToIdentityMatrix(x, y);
  }
  if (IsRotated270(type)) {
    return TransformMatrix{0, -1, x, 1, 0, y};
  }
  if (IsRotated180(type)) {
    return TransformMatrix{
        -1, 0, x, 0, -1, y,
    };
  }
  if (IsRotated90(type)) {
    return TransformMatrix{
        0, 1, x, -1, 0, y,
    };
  }
  return ApplyPosToIdentityMatrix(x, y);
}
}  // namespace transform

namespace layer {
namespace {
using LayerDecoder = protos::pbzero::LayerProto::Decoder;

const int LAYER_FLAG_HIDDEN = 0x01;
const int OFFSCREEN_LAYER_ROOT_ID = 0x7ffffffd;
}  // namespace

inline bool IsRootLayer(const LayerDecoder& layer) {
  return !layer.has_parent() || layer.parent() == -1;
}

inline bool IsHiddenByPolicy(const LayerDecoder& layer) {
  return (((layer.flags() & LAYER_FLAG_HIDDEN) != 0x0) ||
          (layer.id() == OFFSCREEN_LAYER_ROOT_ID));
}

inline Rect GetBounds(const LayerDecoder& layer) {
  auto bounds = protos::pbzero::FloatRectProto::Decoder(layer.bounds());
  return Rect(bounds);
}

// Returns the screen bounds of a layer, cropped by the size of the crop rect if
// provided, usually given as the layer's associated display.
inline std::optional<geometry::Rect> GetCroppedScreenBounds(
    const LayerDecoder& layer,
    std::optional<geometry::Rect> crop) {
  if (!layer.has_screen_bounds()) {
    return std::nullopt;
  }
  auto screen_bounds =
      protos::pbzero::FloatRectProto::Decoder(layer.screen_bounds());
  auto screen_bounds_rect = Rect(screen_bounds);

  if (crop.has_value() && !(crop->IsEmpty())) {
    screen_bounds_rect = screen_bounds_rect.CropRect(crop.value());
  }
  return screen_bounds_rect;
}

// Reconstructs a layer's transform matrix from available proto data.
inline TransformMatrix GetTransformMatrix(const LayerDecoder& layer_decoder) {
  TransformMatrix matrix;

  if (layer_decoder.has_position()) {
    protos::pbzero::PositionProto::Decoder position(layer_decoder.position());
    matrix.tx = static_cast<double>(position.x());
    matrix.ty = static_cast<double>(position.y());
  }

  if (layer_decoder.has_transform()) {
    TransformDecoder transform(layer_decoder.transform());

    auto type = transform.type();

    if (transform::IsSimpleTransform(type)) {
      matrix = transform::GetTransformMatrix(type, matrix.tx, matrix.ty);
    } else {
      matrix.dsdx = static_cast<double>(transform.dsdx());
      matrix.dtdx = static_cast<double>(transform.dtdx());
      matrix.dsdy = static_cast<double>(transform.dtdy());
      matrix.dtdy = static_cast<double>(transform.dsdy());
    }
  }
  return matrix;
}

// Constructs corner radii from available proto data.
inline geometry::CornerRadii GetCornerRadii(const LayerDecoder& layer) {
  geometry::CornerRadii corner_radii;

  bool has_corner_radii = false;
  if (layer.has_corner_radii()) {
    protos::pbzero::CornerRadiiProto::Decoder radii_decoder(
        layer.corner_radii());
    if (radii_decoder.tl() > 0 || radii_decoder.tr() > 0 ||
        radii_decoder.bl() > 0 || radii_decoder.br() > 0) {
      has_corner_radii = true;
      corner_radii.tl = static_cast<double>(radii_decoder.tl());
      corner_radii.tr = static_cast<double>(radii_decoder.tr());
      corner_radii.bl = static_cast<double>(radii_decoder.bl());
      corner_radii.br = static_cast<double>(radii_decoder.br());
    }
  }
  if (!has_corner_radii && layer.has_corner_radius()) {
    auto radius = static_cast<double>(layer.corner_radius());
    corner_radii.tl = radius;
    corner_radii.tr = radius;
    corner_radii.bl = radius;
    corner_radii.br = radius;
  }

  return corner_radii;
}
}  // namespace layer

namespace display {
namespace {
using DisplayDecoder = protos::pbzero::DisplayProto::Decoder;
}

inline Rect MakeLayerStackSpaceRect(const DisplayDecoder& display_decoder) {
  protos::pbzero::RectProto::Decoder layer_stack_space_rect(
      display_decoder.layer_stack_space_rect());
  return Rect(layer_stack_space_rect);
}

// Reconstructs a display's transform matrix from available proto data.
inline TransformMatrix GetTransformMatrix(
    const DisplayDecoder& display_decoder) {
  TransformMatrix matrix;

  if (display_decoder.has_transform()) {
    TransformDecoder transform(display_decoder.transform());
    auto type = transform.type();

    if (transform::IsSimpleTransform(type)) {
      matrix = transform::GetTransformMatrix(type, 0, 0);
    } else {
      matrix.dsdx = static_cast<double>(transform.dsdx());
      matrix.dtdx = static_cast<double>(transform.dtdx());
      matrix.dsdy = static_cast<double>(transform.dtdy());
      matrix.dtdy = static_cast<double>(transform.dsdy());
    }
  }

  return matrix;
}

// Returns a display's size, rotated if the display's transform is a rotation.
inline geometry::Size GetDisplaySize(const DisplayDecoder& display_decoder) {
  if (!display_decoder.has_size()) {
    return geometry::Size{0, 0};
  }
  protos::pbzero::SizeProto::Decoder size_decoder(display_decoder.size());
  auto w = static_cast<double>(size_decoder.w());
  auto h = static_cast<double>(size_decoder.h());

  if (display_decoder.has_transform()) {
    TransformDecoder transform_decoder(display_decoder.transform());
    auto transform_type = transform_decoder.type();
    if (transform::IsRotated90(transform_type) ||
        transform::IsRotated270(transform_type)) {
      return geometry::Size{h, w};
    }
  }
  return geometry::Size{w, h};
}
}  // namespace display

}  // namespace perfetto::trace_processor::winscope::surfaceflinger_layers

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_UTILS_H_
