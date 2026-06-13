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

#include "src/trace_processor/importers/proto/winscope/windowmanager_parser.h"

#include "perfetto/ext/base/base64.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/android/windowmanager.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/proto/winscope/windowmanager_hierarchy_walker.h"
#include "src/trace_processor/importers/proto/winscope/windowmanager_proto_clone.h"
#include "src/trace_processor/tables/winscope_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/winscope_proto_mapping.h"

namespace perfetto::trace_processor::winscope {

WindowManagerParser::WindowManagerParser(WinscopeContext* context)
    : context_{context},
      hierarchy_walker_{
          context_->trace_processor_context_->storage->mutable_string_pool()},
      args_parser_{*context->trace_processor_context_->descriptor_pool_} {}

void WindowManagerParser::Parse(int64_t timestamp, protozero::ConstBytes blob) {
  protos::pbzero::WindowManagerTraceEntry::Decoder entry_decoder(blob);

  auto snapshot_id = InsertSnapshotRow(timestamp, entry_decoder);

  auto result = hierarchy_walker_.ExtractWindowContainers(entry_decoder);
  if (result.has_parse_error) {
    context_->trace_processor_context_->storage->IncrementStats(
        stats::winscope_windowmanager_parse_errors);
    return;
  }

  InsertWindowContainerRows(timestamp, snapshot_id, result.window_containers);
}

tables::WindowManagerTable::Id WindowManagerParser::InsertSnapshotRow(
    int64_t timestamp,
    protos::pbzero::WindowManagerTraceEntry::Decoder& entry_decoder) {
  const auto pruned_entry_proto =
      windowmanager_proto_clone::CloneEntryProtoPruningChildren(entry_decoder);
  protozero::ConstBytes pruned_proto_bytes{pruned_entry_proto.data(),
                                           pruned_entry_proto.size()};

  auto* trace_processor_context = context_->trace_processor_context_;
  tables::WindowManagerTable::Row row;
  row.ts = timestamp;
  protos::pbzero::WindowManagerTraceEntry::Decoder entry(pruned_proto_bytes);
  row.has_invalid_elapsed_ts = entry.elapsed_realtime_nanos() == 0;
  row.base64_proto_id =
      trace_processor_context->storage->mutable_string_pool()
          ->InternString(base::StringView(base::Base64Encode(
              pruned_proto_bytes.data, pruned_proto_bytes.size)))
          .raw_id();
  protos::pbzero::WindowManagerServiceDumpProto::Decoder service(
      entry_decoder.window_manager_service());
  row.focused_display_id = static_cast<uint32_t>(service.focused_display_id());
  auto row_id = trace_processor_context->storage->mutable_windowmanager_table()
                    ->Insert(row)
                    .id;

  ArgsTracker tracker(trace_processor_context);
  auto inserter = tracker.AddArgsTo(row_id);
  ArgsParser writer(timestamp, inserter, *trace_processor_context->storage);
  base::Status status =
      args_parser_.ParseMessage(pruned_proto_bytes,
                                *util::winscope_proto_mapping::GetProtoName(
                                    tables::WindowManagerTable::Name()),
                                nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    trace_processor_context->storage->IncrementStats(
        stats::winscope_windowmanager_parse_errors);
  }

  return row_id;
}

void WindowManagerParser::InsertWindowContainerRows(
    int64_t timestamp,
    tables::WindowManagerTable::Id snapshot_id,
    const std::vector<WindowManagerHierarchyWalker::ExtractedWindowContainer>&
        window_containers) {
  for (const auto& window_container : window_containers) {
    tables::WindowManagerWindowContainerTable::Row row;

    row.snapshot_id = snapshot_id;
    row.title = window_container.title;
    row.token = window_container.token;
    row.parent_token = window_container.parent_token;
    row.child_index = window_container.child_index;
    row.is_visible = window_container.is_visible;
    row.container_type = window_container.container_type;
    row.name_override = window_container.name_override;

    if (window_container.rect) {
      row.window_rect_id = InsertRectRows(*window_container.rect);
    }

    row.base64_proto_id =
        context_->trace_processor_context_->storage->mutable_string_pool()
            ->InternString(base::StringView(
                base::Base64Encode(window_container.pruned_proto.data(),
                                   window_container.pruned_proto.size())))
            .raw_id();

    auto row_id = context_->trace_processor_context_->storage
                      ->mutable_windowmanager_windowcontainer_table()
                      ->Insert(row)
                      .id;

    InsertWindowContainerArgs(timestamp, row_id, window_container);
  }
}

tables::WinscopeTraceRectTable::Id WindowManagerParser::InsertRectRows(
    const WindowManagerHierarchyWalker::ExtractedRect& rect) {
  tables::WinscopeRectTable::Row rect_row;
  rect_row.x = rect.x;
  rect_row.y = rect.y;
  rect_row.w = rect.w;
  rect_row.h = rect.h;

  auto rect_id =
      context_->trace_processor_context_->storage->mutable_winscope_rect_table()
          ->Insert(rect_row)
          .id;

  tables::WinscopeTraceRectTable::Row trace_rect_row;
  trace_rect_row.rect_id = rect_id;
  trace_rect_row.group_id = static_cast<uint32_t>(rect.display_id);
  trace_rect_row.depth = rect.depth;
  trace_rect_row.is_spy = false;
  trace_rect_row.is_visible = rect.is_visible;
  trace_rect_row.opacity = rect.opacity;
  trace_rect_row.transform_id = MaybeInsertIdentityTransformRow();

  return context_->trace_processor_context_->storage
      ->mutable_winscope_trace_rect_table()
      ->Insert(trace_rect_row)
      .id;
}

tables::WinscopeTransformTable::Id
WindowManagerParser::MaybeInsertIdentityTransformRow() {
  if (transform_id_) {
    return *transform_id_;
  }

  tables::WinscopeTransformTable::Row row;
  row.dsdx = 1;
  row.dsdy = 0;
  row.dtdx = 0;
  row.dtdy = 1;
  row.tx = 0;
  row.ty = 0;
  transform_id_ = context_->trace_processor_context_->storage
                      ->mutable_winscope_transform_table()
                      ->Insert(row)
                      .id;

  return *transform_id_;
}

void WindowManagerParser::InsertWindowContainerArgs(
    int64_t timestamp,
    tables::WindowManagerWindowContainerTable::Id row_id,
    const WindowManagerHierarchyWalker::ExtractedWindowContainer&
        window_container) {
  bool is_root = !window_container.parent_token.has_value();
  const char* proto_name = is_root
                               ? ".perfetto.protos.RootWindowContainerProto"
                               : ".perfetto.protos.WindowContainerChildProto";
  protozero::ConstBytes bytes{window_container.pruned_proto.data(),
                              window_container.pruned_proto.size()};
  ArgsTracker tracker(context_->trace_processor_context_);

  auto inserter = tracker.AddArgsTo(row_id);
  ArgsParser writer(timestamp, inserter,
                    *context_->trace_processor_context_->storage);

  base::Status status = args_parser_.ParseMessage(
      bytes, proto_name, nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    context_->trace_processor_context_->storage->IncrementStats(
        stats::winscope_windowmanager_parse_errors);
  }
}

}  // namespace perfetto::trace_processor::winscope
