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

#include "perfetto/ext/base/base64.h"
#include "protos/perfetto/trace/android/viewcapture.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/tables/winscope_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/winscope_proto_mapping.h"

namespace perfetto {
namespace trace_processor {

ViewCaptureParser::ViewCaptureParser(TraceProcessorContext* context)
    : context_{context}, args_parser_{*context->descriptor_pool_} {}

void ViewCaptureParser::Parse(int64_t timestamp,
                              protozero::ConstBytes blob,
                              PacketSequenceStateGeneration* seq_state) {
  protos::pbzero::ViewCapture::Decoder snapshot_decoder(blob);
  tables::ViewCaptureTable::Row row;
  row.ts = timestamp;
  row.base64_proto_id = context_->storage->mutable_string_pool()
                            ->InternString(base::StringView(
                                base::Base64Encode(blob.data, blob.size)))
                            .raw_id();
  auto snapshot_id =
      context_->storage->mutable_viewcapture_table()->Insert(row).id;

  auto inserter = context_->args_tracker->AddArgsTo(snapshot_id);
  ViewCaptureArgsParser writer(timestamp, inserter, *context_->storage,
                               seq_state);
  const auto table_name = tables::ViewCaptureTable::Name();
  auto allowed_fields =
      util::winscope_proto_mapping::GetAllowedFields(table_name);
  base::Status status = args_parser_.ParseMessage(
      blob, *util::winscope_proto_mapping::GetProtoName(table_name),
      &allowed_fields.value(), writer);

  AddDeinternedData(writer, row.base64_proto_id.value());
  if (!status.ok()) {
    context_->storage->IncrementStats(stats::winscope_viewcapture_parse_errors);
  }
  for (auto it = snapshot_decoder.views(); it; ++it) {
    ParseView(timestamp, *it, snapshot_id, seq_state);
  }
}

void ViewCaptureParser::ParseView(int64_t timestamp,
                                  protozero::ConstBytes blob,
                                  tables::ViewCaptureTable::Id snapshot_id,
                                  PacketSequenceStateGeneration* seq_state) {
  tables::ViewCaptureViewTable::Row view;
  view.snapshot_id = snapshot_id;
  view.base64_proto_id = context_->storage->mutable_string_pool()
                             ->InternString(base::StringView(
                                 base::Base64Encode(blob.data, blob.size)))
                             .raw_id();
  auto layer_id =
      context_->storage->mutable_viewcapture_view_table()->Insert(view).id;

  ArgsTracker tracker(context_);
  auto inserter = tracker.AddArgsTo(layer_id);
  ViewCaptureArgsParser writer(timestamp, inserter, *context_->storage,
                               seq_state);
  base::Status status =
      args_parser_.ParseMessage(blob,
                                *util::winscope_proto_mapping::GetProtoName(
                                    tables::ViewCaptureViewTable::Name()),
                                nullptr /* parse all fields */, writer);

  AddDeinternedData(writer, view.base64_proto_id.value());
  if (!status.ok()) {
    context_->storage->IncrementStats(stats::winscope_viewcapture_parse_errors);
  }
}

void ViewCaptureParser::AddDeinternedData(const ViewCaptureArgsParser& writer,
                                          uint32_t base64_proto_id) {
  auto* deinterned_data_table =
      context_->storage->mutable_viewcapture_interned_data_table();
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

}  // namespace trace_processor
}  // namespace perfetto
