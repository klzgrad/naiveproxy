/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/winscope/android_input_event_parser.h"

#include "perfetto/ext/base/base64.h"
#include "protos/perfetto/trace/android/android_input_event.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/android_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/winscope_proto_mapping.h"

namespace perfetto::trace_processor {

using perfetto::protos::pbzero::AndroidInputEvent;
using perfetto::protos::pbzero::AndroidKeyEvent;
using perfetto::protos::pbzero::AndroidMotionEvent;
using perfetto::protos::pbzero::AndroidWindowInputDispatchEvent;
using perfetto::protos::pbzero::TracePacket;

AndroidInputEventParser::AndroidInputEventParser(TraceProcessorContext* context)
    : context_(*context), args_parser_{*context->descriptor_pool_} {}

void AndroidInputEventParser::ParseAndroidInputEvent(
    int64_t packet_ts,
    const protozero::ConstBytes& bytes) {
  auto input_event = AndroidInputEvent::Decoder(bytes);

  constexpr static auto supported_fields = std::array{
      AndroidInputEvent::kDispatcherMotionEventFieldNumber,
      AndroidInputEvent::kDispatcherMotionEventRedactedFieldNumber,
      AndroidInputEvent::kDispatcherKeyEventFieldNumber,
      AndroidInputEvent::kDispatcherKeyEventRedactedFieldNumber,
      AndroidInputEvent::kDispatcherWindowDispatchEventFieldNumber,
      AndroidInputEvent::kDispatcherWindowDispatchEventRedactedFieldNumber};

  for (auto sub_field_id : supported_fields) {
    auto sub_field = input_event.Get(static_cast<uint32_t>(sub_field_id));
    if (!sub_field.valid())
      continue;

    switch (sub_field_id) {
      case AndroidInputEvent::kDispatcherMotionEventFieldNumber:
      case AndroidInputEvent::kDispatcherMotionEventRedactedFieldNumber:
        ParseMotionEvent(packet_ts, sub_field.as_bytes());
        return;
      case AndroidInputEvent::kDispatcherKeyEventFieldNumber:
      case AndroidInputEvent::kDispatcherKeyEventRedactedFieldNumber:
        ParseKeyEvent(packet_ts, sub_field.as_bytes());
        return;
      case AndroidInputEvent::kDispatcherWindowDispatchEventFieldNumber:
      case AndroidInputEvent::kDispatcherWindowDispatchEventRedactedFieldNumber:
        ParseWindowDispatchEvent(packet_ts, sub_field.as_bytes());
        return;
    }
  }
}

void AndroidInputEventParser::ParseMotionEvent(
    int64_t packet_ts,
    const protozero::ConstBytes& bytes) {
  AndroidMotionEvent::Decoder event_proto(bytes);
  tables::AndroidMotionEventsTable::Row event_row;
  event_row.event_id = event_proto.event_id();
  event_row.ts = packet_ts;
  event_row.base64_proto_id =
      context_.storage->mutable_string_pool()
          ->InternString(
              base::StringView(base::Base64Encode(bytes.data, bytes.size)))
          .raw_id();
  event_row.source = event_proto.source();
  event_row.action = event_proto.action();
  event_row.device_id = event_proto.device_id();
  event_row.display_id = event_proto.display_id();

  auto event_row_id = context_.storage->mutable_android_motion_events_table()
                          ->Insert(event_row)
                          .id;
  ArgsTracker args_tracker(&context_);
  auto inserter = args_tracker.AddArgsTo(event_row_id);
  ArgsParser writer{packet_ts, inserter, *context_.storage};

  base::Status status =
      args_parser_.ParseMessage(bytes,
                                *util::winscope_proto_mapping::GetProtoName(
                                    tables::AndroidMotionEventsTable::Name()),
                                nullptr /*parse all fields*/, writer);
  if (!status.ok())
    context_.storage->IncrementStats(stats::android_input_event_parse_errors);
}

void AndroidInputEventParser::ParseKeyEvent(
    int64_t packet_ts,
    const protozero::ConstBytes& bytes) {
  AndroidKeyEvent::Decoder event_proto(bytes);
  tables::AndroidKeyEventsTable::Row event_row;
  event_row.event_id = event_proto.event_id();
  event_row.ts = packet_ts;
  event_row.base64_proto_id =
      context_.storage->mutable_string_pool()
          ->InternString(
              base::StringView(base::Base64Encode(bytes.data, bytes.size)))
          .raw_id();
  event_row.source = event_proto.source();
  event_row.action = event_proto.action();
  event_row.device_id = event_proto.device_id();
  event_row.display_id = event_proto.display_id();
  event_row.key_code = event_proto.key_code();

  auto event_row_id = context_.storage->mutable_android_key_events_table()
                          ->Insert(event_row)
                          .id;
  ArgsTracker args_tracker(&context_);
  auto inserter = args_tracker.AddArgsTo(event_row_id);
  ArgsParser writer{packet_ts, inserter, *context_.storage};

  base::Status status =
      args_parser_.ParseMessage(bytes,
                                *util::winscope_proto_mapping::GetProtoName(
                                    tables::AndroidKeyEventsTable::Name()),
                                nullptr /*parse all fields*/, writer);
  if (!status.ok())
    context_.storage->IncrementStats(stats::android_input_event_parse_errors);
}

void AndroidInputEventParser::ParseWindowDispatchEvent(
    int64_t packet_ts,
    const protozero::ConstBytes& bytes) {
  AndroidWindowInputDispatchEvent::Decoder event_proto(bytes);
  tables::AndroidInputEventDispatchTable::Row event_row;
  event_row.event_id = event_proto.event_id();
  event_row.vsync_id = event_proto.vsync_id();
  event_row.window_id = event_proto.window_id();
  event_row.base64_proto_id =
      context_.storage->mutable_string_pool()
          ->InternString(
              base::StringView(base::Base64Encode(bytes.data, bytes.size)))
          .raw_id();

  auto event_row_id =
      context_.storage->mutable_android_input_event_dispatch_table()
          ->Insert(event_row)
          .id;

  ArgsTracker args_tracker(&context_);
  auto inserter = args_tracker.AddArgsTo(event_row_id);
  ArgsParser writer{packet_ts, inserter, *context_.storage};

  base::Status status = args_parser_.ParseMessage(
      bytes,
      *util::winscope_proto_mapping::GetProtoName(
          tables::AndroidInputEventDispatchTable::Name()),
      nullptr /*parse all fields*/, writer);
  if (!status.ok())
    context_.storage->IncrementStats(stats::android_input_event_parse_errors);
}

}  // namespace perfetto::trace_processor
