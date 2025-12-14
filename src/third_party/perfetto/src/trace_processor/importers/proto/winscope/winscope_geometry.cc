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

#include "src/trace_processor/importers/proto/winscope/winscope_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace perfetto::trace_processor::winscope::geometry {
namespace {
const double CLOSE_THRESHOLD = 0.01;
const double EQUAL_THRESHOLD = 0.000001;

bool IsEqual(double a, double b) {
  return std::abs(a - b) < EQUAL_THRESHOLD;
}

bool IsClose(double a, double b) {
  return std::abs(a - b) < CLOSE_THRESHOLD;
}

bool IsApproxLe(double a, double b) {
  return a <= b || IsClose(a, b);
}

// Checks if (x, y) is within circle centered at (cx, cy) with radius r.
// Used to determine if a rect is completely contained within another
// when accounting for corner radii.
bool IsPointInCircle(Point point, Point center, double r) {
  double dx = point.x - center.x;
  double dy = point.y - center.y;
  return dx * dx + dy * dy <= r * r + CLOSE_THRESHOLD;
}
}  // namespace

Rect::Rect() = default;

Rect::Rect(const protos::pbzero::RectProto::Decoder& rect) {
  x = rect.has_left() ? rect.left() : 0;
  y = rect.has_top() ? rect.top() : 0;
  auto right = rect.has_right() ? rect.right() : 0;
  auto bottom = rect.has_bottom() ? rect.bottom() : 0;
  w = right - x;
  h = bottom - y;
}

Rect::Rect(const protos::pbzero::FloatRectProto::Decoder& rect) {
  x = rect.has_left() ? static_cast<double>(rect.left()) : 0;
  y = rect.has_top() ? static_cast<double>(rect.top()) : 0;
  auto right = rect.has_right() ? static_cast<double>(rect.right()) : 0;
  auto bottom = rect.has_bottom() ? static_cast<double>(rect.bottom()) : 0;
  w = right - x;
  h = bottom - y;
}

Rect::Rect(double left, double top, double right, double bottom) {
  x = left;
  y = top;
  w = right - left;
  h = bottom - top;
}

bool Rect::IsEmpty() const {
  return w <= 0 || h <= 0;
}

Rect Rect::CropRect(const Rect& other) const {
  const auto max_left = std::max(x, other.x);
  const auto min_right = std::min(x + w, other.x + other.w);
  const auto max_top = std::max(y, other.y);
  const auto min_bottom = std::min(y + h, other.y + other.h);
  return Rect(max_left, max_top, min_right, min_bottom);
}

// Checks if rect contains another rect, accounting for individual corner radii
// of each rect.
bool Rect::ContainsRect(const Rect& inner) const {
  if (IsEmpty()) {
    return false;
  }

  auto right = x + w;
  auto inner_right = inner.x + inner.w;
  auto bottom = y + h;
  auto inner_bottom = inner.y + inner.h;

  auto bounding_box_contained =
      IsApproxLe(x, inner.x) && IsApproxLe(y, inner.y) &&
      IsApproxLe(inner_right, right) && IsApproxLe(inner_bottom, bottom);
  if (!bounding_box_contained) {
    return false;
  }

  // For each corner we check that the center of the inner rect corner's circle
  // is contained within the outer rect corner's circle.

  Point c_o{x + radii.tl, y + radii.tl};
  Point c_i{inner.x + inner.radii.tl, inner.y + inner.radii.tl};
  if (radii.tl > inner.radii.tl && !(c_o.y < c_i.y) && !(c_o.x < c_i.x) &&
      !IsPointInCircle(c_i, c_o, radii.tl)) {
    return false;
  }

  c_o = Point{right - radii.tr, y + radii.tr};
  c_i = Point{inner_right - inner.radii.tr, inner.y + inner.radii.tr};
  if (radii.tr > inner.radii.tr && !(c_o.y < c_i.y) && !(c_o.x >= c_i.x) &&
      !IsPointInCircle(c_i, c_o, radii.tr)) {
    return false;
  }

  c_o = Point{x + radii.bl, bottom - radii.bl};
  c_i = Point{inner.x + inner.radii.bl, inner_bottom - inner.radii.bl};
  if (radii.bl > inner.radii.bl && !(c_o.y >= c_i.y) && !(c_o.x < c_i.x) &&
      !IsPointInCircle(c_i, c_o, radii.bl)) {
    return false;
  }

  c_o = Point{right - radii.br, bottom - radii.br};
  c_i = Point{inner_right - inner.radii.br, inner_bottom - inner.radii.br};
  if (radii.br > inner.radii.br && !(c_o.y >= c_i.y) && !(c_o.x >= c_i.x) &&
      !IsPointInCircle(c_i, c_o, radii.br)) {
    return false;
  }

  return true;
}

bool Rect::IntersectsRect(const Rect& other) const {
  if (x < other.x + other.w && other.x < x + w && y <= other.y + other.h &&
      other.y <= y + h) {
    auto new_x = x;
    auto new_y = y;
    auto new_w = w;
    auto new_h = h;

    if (x < other.x) {
      new_x = other.x;
    }
    if (y < other.y) {
      new_y = other.y;
    }
    if (x + w > other.x + other.w) {
      new_w = other.w;
    }
    if (y + h > other.y + other.h) {
      new_h = other.h;
    }
    return !Rect(new_x, new_y, new_w + new_x, new_h + new_y).IsEmpty();
  }
  return false;
}

bool Rect::operator==(const Rect& other) const {
  return IsEqual(x, other.x) && IsEqual(y, other.y) && IsEqual(w, other.w) &&
         IsEqual(h, other.h);
}

bool Rect::IsAlmostEqual(const Rect& other) const {
  return (IsClose(x, other.x) && IsClose(y, other.y) && IsClose(w, other.w) &&
          IsClose(h, other.h));
}

bool TransformMatrix::operator==(const TransformMatrix& other) const {
  return IsEqual(dsdx, other.dsdx) && IsEqual(dsdy, other.dsdy) &&
         IsEqual(dtdx, other.dtdx) && IsEqual(dtdy, other.dtdy) &&
         IsEqual(tx, other.tx) && IsEqual(ty, other.ty);
}

Point TransformMatrix::TransformPoint(Point point) const {
  return {
      dsdx * point.x + dtdx * point.y + tx,
      dtdy * point.x + dsdy * point.y + ty,
  };
}

Rect TransformMatrix::TransformRect(const Rect& r) const {
  const auto lt_prime = TransformMatrix::TransformPoint({r.x, r.y});
  const auto rb_prime = TransformMatrix::TransformPoint({r.x + r.w, r.y + r.h});
  const auto x = std::min(lt_prime.x, rb_prime.x);
  const auto y = std::min(lt_prime.y, rb_prime.y);
  return Rect(x, y, std::max(lt_prime.x, rb_prime.x),
              std::max(lt_prime.y, rb_prime.y));
}

Region TransformMatrix::TransformRegion(Region region) const {
  std::vector<Rect> rects;
  for (const auto& rect : region.rects) {
    rects.push_back(TransformMatrix::TransformRect(rect));
  }
  return Region{rects};
}

TransformMatrix TransformMatrix::Inverse() const {
  const auto ident = 1.0 / TransformMatrix::Det();
  TransformMatrix inverse = TransformMatrix{
      dsdy * ident, -dtdx * ident, 0, -dtdy * ident, dsdx * ident, 0,
  };
  auto t = inverse.TransformPoint(Point{
      -tx,
      -ty,
  });
  inverse.tx = t.x;
  inverse.ty = t.y;
  return inverse;
}

bool TransformMatrix::IsValid() const {
  return !IsEqual(dsdx * dsdy, dtdx * dtdy);
}

double TransformMatrix::Det() const {
  return dsdx * dsdy - dtdx * dtdy;
}

}  // namespace perfetto::trace_processor::winscope::geometry
