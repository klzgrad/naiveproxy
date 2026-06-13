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

#include "src/trace_processor/importers/proto/winscope/viewcapture_rect_computation.h"

#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor::winscope::viewcapture {

// Helpers used only by this computation.

namespace {

// View depth is increased 4x to emphasise difference in z-position.
const auto DEPTH_MAGNIFICATION = 4;

// Depth and scaling information is stored during the computation for use
// in making child view rects.
struct ViewCaptureRect {
  geometry::Rect rect;
  int32_t depth;
  double new_scale_x;
  double new_scale_y;
  double scroll_x;
  double scroll_y;
};

geometry::Rect MakeRect(const ViewDecoder& view,
                        double left_shift,
                        double top_shift,
                        double scale_x,
                        double scale_y,
                        double new_scale_x,
                        double new_scale_y) {
  double node_left = static_cast<double>(view.left());
  double node_translation_x = static_cast<double>(view.translation_x());
  double node_width = static_cast<double>(view.width());
  double node_top = static_cast<double>(view.top());
  double node_translation_y = static_cast<double>(view.translation_y());
  double node_height = static_cast<double>(view.height());

  auto left = left_shift + (node_left + node_translation_x) * scale_x +
              (node_width * (scale_x - new_scale_x)) / 2;
  auto top = top_shift + (node_top + node_translation_y) * scale_y +
             (node_height * (scale_y - new_scale_y)) / 2;
  auto width = node_width * new_scale_x;
  auto height = node_height * new_scale_y;
  return geometry::Rect(left, top, left + width, top + height);
}
}  // namespace

RectComputation::RectComputation(
    const std::vector<ViewDecoder>& views_top_to_bottom,
    const std::unordered_map<int32_t, bool>& computed_visibility,
    WinscopeRectTracker& rect_tracker)
    : views_top_to_bottom_(views_top_to_bottom),
      computed_visibility_(computed_visibility),
      rect_tracker_(rect_tracker) {}

const std::unordered_map<int32_t, TraceRectTableId> RectComputation::Compute() {
  std::unordered_map<int32_t, ViewCaptureRect> rects;
  std::unordered_map<int32_t, TraceRectTableId> trace_rect_ids;

  for (auto it = views_top_to_bottom_.begin(); it != views_top_to_bottom_.end();
       it++) {
    const ViewDecoder& view = *it;

    double scale_x;
    double scale_y;
    double left_shift;
    double top_shift;
    int32_t depth;

    auto parent_rect_pos = rects.find(view.parent_id());
    if (parent_rect_pos == rects.end()) {
      left_shift = 0;
      top_shift = 0;
      depth = 0;
      scale_x = 1;
      scale_y = 1;
    } else {
      const ViewCaptureRect& parent_rect = parent_rect_pos->second;
      left_shift = parent_rect.rect.x - parent_rect.scroll_x;
      top_shift = parent_rect.rect.y - parent_rect.scroll_y;
      depth = parent_rect.depth + 1;
      scale_x = parent_rect.new_scale_x;
      scale_y = parent_rect.new_scale_y;
    }

    double new_scale_x = scale_x * static_cast<double>(view.scale_x());
    double new_scale_y = scale_y * static_cast<double>(view.scale_y());

    geometry::Rect rect = MakeRect(view, left_shift, top_shift, scale_x,
                                   scale_y, new_scale_x, new_scale_y);
    ViewCaptureRect rect_info{rect,
                              depth,
                              new_scale_x,
                              new_scale_y,
                              static_cast<double>(view.scroll_x()),
                              static_cast<double>(view.scroll_y())};

    int32_t node_id = view.id();
    rects[node_id] = rect_info;
    trace_rect_ids[node_id] = InsertTraceRectRow(view, rect, depth);
  }
  return trace_rect_ids;
}

TraceRectTableId RectComputation::InsertTraceRectRow(const ViewDecoder& view,
                                                     geometry::Rect& rect,
                                                     int32_t depth) {
  tables::WinscopeTraceRectTable::Row row;
  row.rect_id = rect_tracker_.GetOrInsertRow(rect);
  row.group_id = 0;
  row.depth = static_cast<uint32_t>(depth * DEPTH_MAGNIFICATION);
  row.is_visible = computed_visibility_.find(view.id())->second;
  row.opacity = view.alpha();
  return rect_tracker_.context_->storage->mutable_winscope_trace_rect_table()
      ->Insert(row)
      .id;
}
}  // namespace perfetto::trace_processor::winscope::viewcapture
