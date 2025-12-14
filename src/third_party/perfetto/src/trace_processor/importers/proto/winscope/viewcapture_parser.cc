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

#include "src/trace_processor/importers/proto/winscope/viewcapture_parser.h"
#include <sys/types.h>

#include "perfetto/ext/base/base64.h"
#include "protos/perfetto/trace/android/viewcapture.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/proto/winscope/viewcapture_rect_computation.h"
#include "src/trace_processor/importers/proto/winscope/viewcapture_views_extractor.h"
#include "src/trace_processor/importers/proto/winscope/viewcapture_visibility_computation.h"
#include "src/trace_processor/tables/winscope_tables_py.h"

namespace perfetto::trace_processor::winscope {

ViewCaptureParser::ViewCaptureParser(WinscopeContext* context)
    : context_{context},
      args_parser_{*context->trace_processor_context_->descriptor_pool_} {}

void ViewCaptureParser::Parse(int64_t timestamp,
                              protozero::ConstBytes blob,
                              PacketSequenceStateGeneration* seq_state) {
  auto* storage = context_->trace_processor_context_->storage.get();

  protos::pbzero::ViewCapture::Decoder snapshot_decoder(blob);
  tables::ViewCaptureTable::Row row;
  row.ts = timestamp;
  row.base64_proto_id = storage->mutable_string_pool()
                            ->InternString(base::StringView(
                                base::Base64Encode(blob.data, blob.size)))
                            .raw_id();

  auto id_and_row = storage->mutable_viewcapture_table()->Insert(row);
  auto snapshot_id = id_and_row.id;
  auto row_ref = id_and_row.row_reference;

  ArgsTracker args_tracker(context_->trace_processor_context_);
  auto inserter = args_tracker.AddArgsTo(snapshot_id);
  ViewCaptureArgsParser writer(timestamp, inserter, *storage, seq_state,
                               &row_ref, nullptr);
  const auto table_name = tables::ViewCaptureTable::Name();
  auto allowed_fields =
      util::winscope_proto_mapping::GetAllowedFields(table_name);
  base::Status status = args_parser_.ParseMessage(
      blob, *util::winscope_proto_mapping::GetProtoName(table_name),
      &allowed_fields.value(), writer);

  AddDeinternedData(writer, row.base64_proto_id.value());
  if (!status.ok()) {
    storage->IncrementStats(stats::winscope_viewcapture_parse_errors);
  }

  auto views_top_to_bottom =
      viewcapture::ExtractViewsTopToBottom(snapshot_decoder);
  auto computed_visibility =
      viewcapture::VisibilityComputation(views_top_to_bottom).Compute();
  auto computed_rects =
      viewcapture::RectComputation(views_top_to_bottom, computed_visibility,
                                   context_->rect_tracker_)
          .Compute();

  for (auto it = snapshot_decoder.views(); it; ++it) {
    ParseView(timestamp, *it, snapshot_id, seq_state, computed_visibility,
              computed_rects);
  }
}

void ViewCaptureParser::ParseView(
    int64_t timestamp,
    protozero::ConstBytes blob,
    tables::ViewCaptureTable::Id snapshot_id,
    PacketSequenceStateGeneration* seq_state,
    std::unordered_map<int32_t, bool>& computed_visibility,
    std::unordered_map<int32_t, tables::WinscopeTraceRectTable::Id>&
        computed_rects) {
  auto* storage = context_->trace_processor_context_->storage.get();

  tables::ViewCaptureViewTable::Row view;
  view.snapshot_id = snapshot_id;
  view.base64_proto_id = storage->mutable_string_pool()
                             ->InternString(base::StringView(
                                 base::Base64Encode(blob.data, blob.size)))
                             .raw_id();

  protos::pbzero::ViewCapture::View::Decoder view_decoder(blob);
  auto node_id = view_decoder.id();
  view.node_id = static_cast<uint32_t>(node_id);
  view.hashcode = static_cast<uint32_t>(view_decoder.hashcode());
  view.is_visible = computed_visibility.find(node_id)->second;
  view.trace_rect_id = computed_rects.find(node_id)->second;
  view.parent_id = static_cast<uint32_t>(view_decoder.parent_id());

  auto id_and_row = storage->mutable_viewcapture_view_table()->Insert(view);
  auto row_id = id_and_row.id;
  auto row_ref = id_and_row.row_reference;

  ArgsTracker tracker(context_->trace_processor_context_);
  auto inserter = tracker.AddArgsTo(row_id);
  ViewCaptureArgsParser writer(timestamp, inserter, *storage, seq_state,
                               nullptr, &row_ref);
  base::Status status =
      args_parser_.ParseMessage(blob,
                                *util::winscope_proto_mapping::GetProtoName(
                                    tables::ViewCaptureViewTable::Name()),
                                nullptr /* parse all fields */, writer);

  AddDeinternedData(writer, view.base64_proto_id.value());
  if (!status.ok()) {
    storage->IncrementStats(stats::winscope_viewcapture_parse_errors);
  }
}

void ViewCaptureParser::AddDeinternedData(const ViewCaptureArgsParser& writer,
                                          uint32_t base64_proto_id) {
  auto* deinterned_data_table = context_->trace_processor_context_->storage
                                    ->mutable_viewcapture_interned_data_table();
  for (auto i = writer.flat_key_to_iid_args.GetIterator(); i; ++i) {
    const auto& flat_key = i.key();
    ViewCaptureArgsParser::IidToStringMap& iids_to_add = i.value();

    for (auto j = iids_to_add.GetIterator(); j; ++j) {
      const auto iid = j.key();
      const auto& deinterned_value = j.value();

      tables::ViewCaptureInternedDataTable::Row interned_data_row;
      interned_data_row.base64_proto_id = base64_proto_id;
      interned_data_row.flat_key = flat_key;
      interned_data_row.iid = static_cast<int64_t>(iid);
      interned_data_row.deinterned_value = deinterned_value;
      deinterned_data_table->Insert(interned_data_row);
    }
  }
}

}  // namespace perfetto::trace_processor::winscope
