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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_GEOMETRY_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_GEOMETRY_H_

#include "protos/perfetto/trace/android/graphics/rect.pbzero.h"
#include "protos/perfetto/trace/android/surfaceflinger_layers.pbzero.h"

namespace perfetto::trace_processor::winscope::geometry {

// Represents a corner of a 2D rect from a Winscope trace.
struct Point {
  double x;
  double y;
};

// Represents a 2D rect's size.
struct Size {
  double w;
  double h;
};

// Used to represent and manipulate Winscope rect data to perform various
// computations during Winscope data parsing, such as computing SurfaceFlinger
// visibilities. These rects are added to the __intrinsic_winscope_rect table.
class Rect {
 public:
  explicit Rect();
  explicit Rect(const protos::pbzero::RectProto::Decoder& rect);
  explicit Rect(const protos::pbzero::FloatRectProto::Decoder& rect);
  Rect(double left, double top, double right, double bottom);

  bool operator==(const Rect& other) const;

  bool IsAlmostEqual(const Rect& other) const;
  bool IsEmpty() const;
  Rect CropRect(const Rect& other) const;
  bool ContainsRect(const Rect& other) const;
  bool IntersectsRect(const Rect& other) const;

  double x = 0;
  double y = 0;
  double w = 0;
  double h = 0;
};

// Represents a region e.g. visible region, touchable region in SurfaceFlinger.
struct Region {
  std::vector<Rect> rects;
};

// Represents a transform matrix applied to a rect, e.g. in SurfaceFlinger.
// These transforms are added to the __intrinsic_winscope_transform table.
class TransformMatrix {
 public:
  bool operator==(const TransformMatrix& other) const;

  Point TransformPoint(Point point) const;
  Rect TransformRect(const Rect& r) const;
  Region TransformRegion(Region region) const;
  TransformMatrix Inverse() const;
  bool IsValid() const;

  double dsdx = 1;
  double dtdx = 0;
  double tx = 0;
  double dtdy = 0;
  double dsdy = 1;
  double ty = 0;

 private:
  double Det() const;
};

}  // namespace perfetto::trace_processor::winscope::geometry

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_GEOMETRY_H_
