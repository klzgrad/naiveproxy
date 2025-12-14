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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_GEOMETRY_TEST_UTILS_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_GEOMETRY_TEST_UTILS_H_

#include "src/trace_processor/importers/proto/winscope/winscope_geometry.h"

#include "protos/perfetto/trace/android/graphics/rect.gen.h"
#include "protos/perfetto/trace/android/surfaceflinger_common.gen.h"
#include "protos/perfetto/trace/android/surfaceflinger_layers.gen.h"

namespace perfetto::trace_processor::winscope::geometry::test {

using FloatRectProto = protos::gen::FloatRectProto;
using RectProto = protos::gen::RectProto;

inline void UpdateRect(protos::gen::FloatRectProto* rect_proto,
                       geometry::Rect rect) {
  rect_proto->set_left(static_cast<float>(rect.x));
  rect_proto->set_top(static_cast<float>(rect.y));
  rect_proto->set_right(static_cast<float>(rect.x + rect.w));
  rect_proto->set_bottom(static_cast<float>(rect.y + rect.h));
}

inline void UpdateRect(protos::gen::RectProto* rect_proto,
                       geometry::Rect rect) {
  rect_proto->set_left(static_cast<int32_t>(rect.x));
  rect_proto->set_top(static_cast<int32_t>(rect.y));
  rect_proto->set_right(static_cast<int32_t>(rect.x + rect.w));
  rect_proto->set_bottom(static_cast<int32_t>(rect.y + rect.h));
}
}  // namespace perfetto::trace_processor::winscope::geometry::test

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_GEOMETRY_TEST_UTILS_H_
