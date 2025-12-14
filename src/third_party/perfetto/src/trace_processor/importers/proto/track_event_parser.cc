/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/track_event_parser.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/synthetic_tid.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/proto/stack_profile_sequence_state.h"
#include "src/trace_processor/importers/proto/track_event_event_importer.h"
#include "src/trace_processor/importers/proto/track_event_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/debug_annotation_parser.h"
#include "src/trace_processor/util/proto_to_args_parser.h"

#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/track_event/chrome_process_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/chrome_thread_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/process_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/source_location.pbzero.h"
#include "protos/perfetto/trace/track_event/thread_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/track_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"

namespace perfetto::trace_processor {

namespace {
using BoundInserter = ArgsTracker::BoundInserter;
using protos::pbzero::TrackEvent;
using LegacyEvent = TrackEvent::LegacyEvent;
using protozero::ConstBytes;

std::optional<base::Status> MaybeParseUnsymbolizedSourceLocation(
    const std::string& prefix,
    const protozero::Field& field,
    util::ProtoToArgsParser::Delegate& delegate) {
  auto* decoder = delegate.GetInternedMessage(
      protos::pbzero::InternedData::kUnsymbolizedSourceLocations,
      field.as_uint64());
  if (!decoder) {
    // Lookup failed fall back on default behaviour which will just put
    // the iid into the args table.
    return std::nullopt;
  }
  // Interned mapping_id loses it's meaning when the sequence ends. So we need
  // to get an id from stack_profile_mapping table.
  auto* mapping = delegate.seq_state()
                      ->GetCustomState<StackProfileSequenceState>()
                      ->FindOrInsertMapping(decoder->mapping_id());
  if (!mapping) {
    return std::nullopt;
  }
  delegate.AddUnsignedInteger(
      util::ProtoToArgsParser::Key(prefix + ".mapping_id"),
      mapping->mapping_id().value);
  delegate.AddUnsignedInteger(util::ProtoToArgsParser::Key(prefix + ".rel_pc"),
                              decoder->rel_pc());
  return base::OkStatus();
}

std::optional<base::Status> MaybeParseSourceLocation(
    const std::string& prefix,
    const protozero::Field& field,
    util::ProtoToArgsParser::Delegate& delegate) {
  auto* decoder = delegate.GetInternedMessage(
      protos::pbzero::InternedData::kSourceLocations, field.as_uint64());
  if (!decoder) {
    // Lookup failed fall back on default behaviour which will just put
    // the source_location_iid into the args table.
    return std::nullopt;
  }

  delegate.AddString(util::ProtoToArgsParser::Key(prefix + ".file_name"),
                     NormalizePathSeparators(decoder->file_name()));
  delegate.AddString(util::ProtoToArgsParser::Key(prefix + ".function_name"),
                     decoder->function_name());
  if (decoder->has_line_number()) {
    delegate.AddInteger(util::ProtoToArgsParser::Key(prefix + ".line_number"),
                        decoder->line_number());
  }
  return base::OkStatus();
}

}  // namespace

TrackEventParser::TrackEventParser(TraceProcessorContext* context,
                                   TrackEventTracker* track_event_tracker)
    : args_parser_(*context->descriptor_pool_),
      context_(context),
      track_event_tracker_(track_event_tracker),
      counter_name_thread_time_id_(
          context->storage->InternString("thread_time")),
      counter_name_thread_instruction_count_id_(
          context->storage->InternString("thread_instruction_count")),
      task_file_name_args_key_id_(
          context->storage->InternString("task.posted_from.file_name")),
      task_function_name_args_key_id_(
          context->storage->InternString("task.posted_from.function_name")),
      task_line_number_args_key_id_(
          context->storage->InternString("task.posted_from.line_number")),
      log_message_body_key_id_(
          context->storage->InternString("track_event.log_message")),
      log_message_source_location_function_name_key_id_(
          context->storage->InternString(
              "track_event.log_message.function_name")),
      log_message_source_location_file_name_key_id_(
          context->storage->InternString("track_event.log_message.file_name")),
      log_message_source_location_line_number_key_id_(
          context->storage->InternString(
              "track_event.log_message.line_number")),
      log_message_priority_id_(
          context->storage->InternString("track_event.priority")),
      source_location_function_name_key_id_(
          context->storage->InternString("source.function_name")),
      source_location_file_name_key_id_(
          context->storage->InternString("source.file_name")),
      source_location_line_number_key_id_(
          context->storage->InternString("source.line_number")),
      raw_legacy_event_id_(
          context->storage->InternString("track_event.legacy_event")),
      legacy_event_passthrough_utid_id_(
          context->storage->InternString("legacy_event.passthrough_utid")),
      legacy_event_category_key_id_(
          context->storage->InternString("legacy_event.category")),
      legacy_event_name_key_id_(
          context->storage->InternString("legacy_event.name")),
      legacy_event_phase_key_id_(
          context->storage->InternString("legacy_event.phase")),
      legacy_event_duration_ns_key_id_(
          context->storage->InternString("legacy_event.duration_ns")),
      legacy_event_thread_timestamp_ns_key_id_(
          context->storage->InternString("legacy_event.thread_timestamp_ns")),
      legacy_event_thread_duration_ns_key_id_(
          context->storage->InternString("legacy_event.thread_duration_ns")),
      legacy_event_thread_instruction_count_key_id_(
          context->storage->InternString(
              "legacy_event.thread_instruction_count")),
      legacy_event_thread_instruction_delta_key_id_(
          context->storage->InternString(
              "legacy_event.thread_instruction_delta")),
      legacy_event_use_async_tts_key_id_(
          context->storage->InternString("legacy_event.use_async_tts")),
      legacy_event_unscoped_id_key_id_(
          context->storage->InternString("legacy_event.unscoped_id")),
      legacy_event_global_id_key_id_(
          context->storage->InternString("legacy_event.global_id")),
      legacy_event_local_id_key_id_(
          context->storage->InternString("legacy_event.local_id")),
      legacy_event_id_scope_key_id_(
          context->storage->InternString("legacy_event.id_scope")),
      legacy_event_bind_id_key_id_(
          context->storage->InternString("legacy_event.bind_id")),
      legacy_event_bind_to_enclosing_key_id_(
          context->storage->InternString("legacy_event.bind_to_enclosing")),
      legacy_event_flow_direction_key_id_(
          context->storage->InternString("legacy_event.flow_direction")),
      histogram_name_key_id_(
          context->storage->InternString("chrome_histogram_sample.name")),
      flow_direction_value_in_id_(context->storage->InternString("in")),
      flow_direction_value_out_id_(context->storage->InternString("out")),
      flow_direction_value_inout_id_(context->storage->InternString("inout")),
      chrome_legacy_ipc_class_args_key_id_(
          context->storage->InternString("legacy_ipc.class")),
      chrome_legacy_ipc_line_args_key_id_(
          context->storage->InternString("legacy_ipc.line")),
      chrome_host_app_package_name_id_(
          context->storage->InternString("chrome.host_app_package_name")),
      chrome_crash_trace_id_name_id_(
          context->storage->InternString("chrome.crash_trace_id")),
      chrome_process_label_flat_key_id_(
          context->storage->InternString("chrome.process_label")),
      chrome_process_type_id_(
          context_->storage->InternString("chrome.process_type")),
      event_category_key_id_(context_->storage->InternString("event.category")),
      event_name_key_id_(context_->storage->InternString("event.name")),
      correlation_id_key_id_(context->storage->InternString("correlation_id")),
      legacy_trace_source_id_key_id_(
          context_->storage->InternString("legacy_trace_source_id")),
      callsite_id_key_id_(context_->storage->InternString("callsite_id")),
      end_callsite_id_key_id_(
          context_->storage->InternString("end_callsite_id")),
      chrome_string_lookup_(context->storage.get()),
      active_chrome_processes_tracker_(context) {
  args_parser_.AddParsingOverrideForField(
      "chrome_mojo_event_info.mojo_interface_method_iid",
      [](const protozero::Field& field,
         util::ProtoToArgsParser::Delegate& delegate) {
        return MaybeParseUnsymbolizedSourceLocation(
            "chrome_mojo_event_info.mojo_interface_method.native_symbol", field,
            delegate);
      });
  // Switch |source_location_iid| into its interned data variant.
  args_parser_.AddParsingOverrideForField(
      "begin_impl_frame_args.current_args.source_location_iid",
      [](const protozero::Field& field,
         util::ProtoToArgsParser::Delegate& delegate) {
        return MaybeParseSourceLocation("begin_impl_frame_args.current_args",
                                        field, delegate);
      });
  args_parser_.AddParsingOverrideForField(
      "begin_impl_frame_args.last_args.source_location_iid",
      [](const protozero::Field& field,
         util::ProtoToArgsParser::Delegate& delegate) {
        return MaybeParseSourceLocation("begin_impl_frame_args.last_args",
                                        field, delegate);
      });
  args_parser_.AddParsingOverrideForField(
      "begin_frame_observer_state.last_begin_frame_args.source_location_iid",
      [](const protozero::Field& field,
         util::ProtoToArgsParser::Delegate& delegate) {
        return MaybeParseSourceLocation(
            "begin_frame_observer_state.last_begin_frame_args", field,
            delegate);
      });
  args_parser_.AddParsingOverrideForField(
      "chrome_memory_pressure_notification.creation_location_iid",
      [](const protozero::Field& field,
         util::ProtoToArgsParser::Delegate& delegate) {
        return MaybeParseSourceLocation("chrome_memory_pressure_notification",
                                        field, delegate);
      });

  // Parse DebugAnnotations.
  args_parser_.AddParsingOverrideForType(
      ".perfetto.protos.DebugAnnotation",
      [&](util::ProtoToArgsParser::ScopedNestedKeyContext& key,
          const protozero::ConstBytes& data,
          util::ProtoToArgsParser::Delegate& delegate) {
        // Do not add "debug_annotations" to the final key.
        key.RemoveFieldSuffix();
        util::DebugAnnotationParser annotation_parser(args_parser_);
        return annotation_parser.Parse(data, delegate);
      });

  args_parser_.AddParsingOverrideForField(
      "active_processes.pid", [&](const protozero::Field& field,
                                  util::ProtoToArgsParser::Delegate& delegate) {
        AddActiveProcess(delegate.packet_timestamp(), field.as_int32());
        // Fallthrough so that the parser adds pid as a regular arg.
        return std::nullopt;
      });

  for (uint16_t index : kReflectFields) {
    reflect_fields_.push_back(index);
  }
}

void TrackEventParser::ParseTrackDescriptor(
    int64_t packet_timestamp,
    protozero::ConstBytes track_descriptor,
    uint32_t) {
  protos::pbzero::TrackDescriptor::Decoder decoder(track_descriptor);

  // Ensure that the track and its parents are resolved. This may start a new
  // process and/or thread (i.e. new upid/utid).
  auto track = track_event_tracker_->ResolveDescriptorTrack(decoder.uuid());
  if (!track) {
    context_->storage->IncrementStats(stats::track_event_parser_errors);
    return;
  }

  if (decoder.has_thread()) {
    if (decoder.has_chrome_thread()) {
      protos::pbzero::ChromeThreadDescriptor::Decoder chrome_decoder(
          decoder.chrome_thread());
      bool is_sandboxed = chrome_decoder.has_is_sandboxed_tid() &&
                          chrome_decoder.is_sandboxed_tid();
      UniqueTid utid = ParseThreadDescriptor(decoder.thread(), is_sandboxed);
      ParseChromeThreadDescriptor(utid, decoder.chrome_thread());
    } else {
      ParseThreadDescriptor(decoder.thread(), /*is_sandboxed=*/false);
    }
  } else if (decoder.has_process()) {
    UniquePid upid =
        ParseProcessDescriptor(packet_timestamp, decoder.process());
    if (decoder.has_chrome_process()) {
      ParseChromeProcessDescriptor(upid, decoder.chrome_process());
    }
  }
}

UniquePid TrackEventParser::ParseProcessDescriptor(
    int64_t packet_timestamp,
    protozero::ConstBytes process_descriptor) {
  protos::pbzero::ProcessDescriptor::Decoder decoder(process_descriptor);
  UniquePid upid = context_->process_tracker->GetOrCreateProcess(
      static_cast<uint32_t>(decoder.pid()));
  active_chrome_processes_tracker_.AddProcessDescriptor(packet_timestamp, upid);
  if (decoder.has_process_name() && decoder.process_name().size) {
    // Don't override system-provided names.
    context_->process_tracker->SetProcessNameIfUnset(
        upid, context_->storage->InternString(decoder.process_name()));
  }
  if (decoder.has_start_timestamp_ns() && decoder.start_timestamp_ns() > 0) {
    context_->process_tracker->SetStartTsIfUnset(upid,
                                                 decoder.start_timestamp_ns());
  }
  // TODO(skyostil): Remove parsing for legacy chrome_process_type field.
  if (decoder.has_chrome_process_type()) {
    StringId name_id =
        chrome_string_lookup_.GetProcessName(decoder.chrome_process_type());
    // Don't override system-provided names.
    context_->process_tracker->SetProcessNameIfUnset(upid, name_id);
  }
  int label_index = 0;
  for (auto it = decoder.process_labels(); it; it++) {
    StringId label_id = context_->storage->InternString(*it);
    std::string key = "chrome.process_label[";
    key.append(std::to_string(label_index++));
    key.append("]");
    context_->process_tracker->AddArgsToProcess(upid).AddArg(
        chrome_process_label_flat_key_id_,
        context_->storage->InternString(base::StringView(key)),
        Variadic::String(label_id));
  }
  return upid;
}

void TrackEventParser::ParseChromeProcessDescriptor(
    UniquePid upid,
    protozero::ConstBytes chrome_process_descriptor) {
  protos::pbzero::ChromeProcessDescriptor::Decoder decoder(
      chrome_process_descriptor);

  StringId name_id =
      chrome_string_lookup_.GetProcessName(decoder.process_type());
  // Don't override system-provided names.
  context_->process_tracker->SetProcessNameIfUnset(upid, name_id);

  ArgsTracker::BoundInserter process_args =
      context_->process_tracker->AddArgsToProcess(upid);
  // For identifying Chrome processes in system traces.
  process_args.AddArg(chrome_process_type_id_, Variadic::String(name_id));
  if (decoder.has_host_app_package_name()) {
    process_args.AddArg(chrome_host_app_package_name_id_,
                        Variadic::String(context_->storage->InternString(
                            decoder.host_app_package_name())));
  }
  if (decoder.has_crash_trace_id()) {
    process_args.AddArg(chrome_crash_trace_id_name_id_,
                        Variadic::UnsignedInteger(decoder.crash_trace_id()));
  }
}

UniqueTid TrackEventParser::ParseThreadDescriptor(
    protozero::ConstBytes thread_descriptor,
    bool is_sandboxed) {
  protos::pbzero::ThreadDescriptor::Decoder decoder(thread_descriptor);
  // TODO: b/175152326 - Should pid namespace translation also be done here?
  auto pid = static_cast<int64_t>(decoder.pid());
  auto tid = static_cast<int64_t>(decoder.tid());
  // If tid is sandboxed then use a unique synthetic tid, to avoid
  // having concurrent threads with the same tid.
  if (is_sandboxed) {
    tid = CreateSyntheticTid(tid, pid);
  }
  UniqueTid utid = context_->process_tracker->UpdateThread(tid, pid);
  StringId name_id = kNullStringId;
  if (decoder.has_thread_name() && decoder.thread_name().size) {
    name_id = context_->storage->InternString(decoder.thread_name());
  } else if (decoder.has_chrome_thread_type()) {
    // TODO(skyostil): Remove parsing for legacy chrome_thread_type field.
    name_id = chrome_string_lookup_.GetThreadName(decoder.chrome_thread_type());
  }
  context_->process_tracker->UpdateThreadName(
      utid, name_id, ThreadNamePriority::kTrackDescriptor);
  return utid;
}

void TrackEventParser::ParseChromeThreadDescriptor(
    UniqueTid utid,
    protozero::ConstBytes chrome_thread_descriptor) {
  protos::pbzero::ChromeThreadDescriptor::Decoder decoder(
      chrome_thread_descriptor);
  if (!decoder.has_thread_type())
    return;

  StringId name_id = chrome_string_lookup_.GetThreadName(decoder.thread_type());
  context_->process_tracker->UpdateThreadName(
      utid, name_id, ThreadNamePriority::kTrackDescriptorThreadType);
}

void TrackEventParser::ParseTrackEvent(int64_t ts,
                                       const TrackEventData* event_data,
                                       ConstBytes blob,
                                       uint32_t packet_sequence_id) {
  const auto range_of_interest_start_us =
      track_event_tracker_->range_of_interest_start_us();
  if (context_->config.drop_track_event_data_before ==
          DropTrackEventDataBefore::kTrackEventRangeOfInterest &&
      range_of_interest_start_us && ts < *range_of_interest_start_us * 1000) {
    // The event is outside of the range of interest, and dropping is enabled.
    // So we drop the event.
    context_->storage->IncrementStats(
        stats::track_event_dropped_packets_outside_of_range_of_interest);
    return;
  }
  base::Status status =
      TrackEventEventImporter(this, ts, event_data, blob, packet_sequence_id)
          .Import();
  if (!status.ok()) {
    context_->storage->IncrementStats(stats::track_event_parser_errors);
    PERFETTO_DLOG("ParseTrackEvent error: %s", status.c_message());
  }
}

void TrackEventParser::AddActiveProcess(int64_t packet_timestamp, int32_t pid) {
  UniquePid upid =
      context_->process_tracker->GetOrCreateProcess(static_cast<uint32_t>(pid));
  active_chrome_processes_tracker_.AddActiveProcessMetadata(packet_timestamp,
                                                            upid);
}

DummyMemoryMapping* TrackEventParser::GetOrCreateInlineCallstackDummyMapping() {
  if (!inline_callstack_dummy_mapping_) {
    inline_callstack_dummy_mapping_ =
        &context_->mapping_tracker->CreateDummyMapping("track_event_inline");
  }
  return inline_callstack_dummy_mapping_;
}

void TrackEventParser::NotifyEndOfFile() {
  active_chrome_processes_tracker_.NotifyEndOfFile();
}

}  // namespace perfetto::trace_processor
