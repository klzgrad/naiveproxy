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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_RECT_COMPUTATION_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_RECT_COMPUTATION_H_

#include <optional>
#include <unordered_map>
#include <vector>
#include "protos/perfetto/trace/android/viewcapture.pbzero.h"
#include "src/trace_processor/importers/proto/winscope/winscope_geometry.h"
#include "src/trace_processor/importers/proto/winscope/winscope_rect_tracker.h"
#include "src/trace_processor/tables/winscope_tables_py.h"

namespace perfetto::trace_processor::winscope::viewcapture {

namespace {
using TraceRectTableId = tables::WinscopeTraceRectTable::Id;
using SnapshotDecoder = protos::pbzero::ViewCapture::Decoder;
using ViewDecoder = protos::pbzero::ViewCapture::View::Decoder;
}  // namespace

struct SurfaceFlingerRects {
  std::optional<TraceRectTableId> layer_rect = std::nullopt;
  std::optional<TraceRectTableId> input_rect = std::nullopt;
};

class RectComputation {
 public:
  explicit RectComputation(
      const std::vector<ViewDecoder>& views_top_to_bottom,
      const std::unordered_map<int32_t, bool>& computed_visibility,
      WinscopeRectTracker& rect_tracker);

  const std::unordered_map<int32_t, TraceRectTableId> Compute();

 private:
  const std::vector<ViewDecoder>& views_top_to_bottom_;
  const std::unordered_map<int32_t, bool>& computed_visibility_;
  WinscopeRectTracker& rect_tracker_;

  TraceRectTableId InsertTraceRectRow(const ViewDecoder& view,
                                      geometry::Rect& rect,
                                      int32_t depth);
};
}  // namespace perfetto::trace_processor::winscope::viewcapture

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_VIEWCAPTURE_RECT_COMPUTATION_H_
