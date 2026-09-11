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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_WINSCOPE_GEOMETRY_TEST_UTILS_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_WINSCOPE_GEOMETRY_TEST_UTILS_H_

#include "src/trace_processor/plugins/winscope_importer/winscope_geometry.h"

#include "protos/third_party/android/frameworks/native/tracing/winscope/common/corner_radii.gen.h"
#include "protos/third_party/android/frameworks/native/tracing/winscope/common/rect.gen.h"
#include "protos/third_party/android/frameworks/native/tracing/winscope/surfaceflinger_common.gen.h"
#include "protos/third_party/android/frameworks/native/tracing/winscope/surfaceflinger_layers.gen.h"

namespace perfetto::trace_processor::winscope::geometry::test {

using FloatRectProto = com::android::internal::gen::FloatRectProto;
using RectProto = com::android::internal::gen::RectProto;

inline void UpdateRect(com::android::internal::gen::FloatRectProto* rect_proto,
                       geometry::Rect rect) {
  rect_proto->set_left(static_cast<float>(rect.x));
  rect_proto->set_top(static_cast<float>(rect.y));
  rect_proto->set_right(static_cast<float>(rect.x + rect.w));
  rect_proto->set_bottom(static_cast<float>(rect.y + rect.h));
}

inline void UpdateRect(com::android::internal::gen::RectProto* rect_proto,
                       geometry::Rect rect) {
  rect_proto->set_left(static_cast<int32_t>(rect.x));
  rect_proto->set_top(static_cast<int32_t>(rect.y));
  rect_proto->set_right(static_cast<int32_t>(rect.x + rect.w));
  rect_proto->set_bottom(static_cast<int32_t>(rect.y + rect.h));
}

inline void UpdateCornerRadii(
    com::android::internal::gen::CornerRadiiProto* corner_radii_proto,
    geometry::CornerRadii corner_radii) {
  corner_radii_proto->set_bl(static_cast<float>(corner_radii.bl));
  corner_radii_proto->set_br(static_cast<float>(corner_radii.br));
  corner_radii_proto->set_tl(static_cast<float>(corner_radii.tl));
  corner_radii_proto->set_tr(static_cast<float>(corner_radii.tr));
}

inline bool IsCornerRadiiEqual(geometry::CornerRadii value,
                               geometry::CornerRadii other) {
  return IsEqual(value.bl, other.bl) && IsEqual(value.br, other.br) &&
         IsEqual(value.tl, other.tl) && IsEqual(value.tr, other.tr);
}
}  // namespace perfetto::trace_processor::winscope::geometry::test

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_WINSCOPE_GEOMETRY_TEST_UTILS_H_
