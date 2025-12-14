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

#include "src/trace_processor/importers/proto/track_event_tokenizer.h"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/legacy_v8_cpu_profile_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/proto_trace_reader.h"
#include "src/trace_processor/importers/proto/track_event_tracker.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/json_utils.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/track_event/chrome_thread_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/counter_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/process_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/range_of_interest.pbzero.h"
#include "protos/perfetto/trace/track_event/thread_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/track_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {
namespace {
using protos::pbzero::CounterDescriptor;

class V8Sink : public TraceSorter::Sink<LegacyV8CpuProfileEvent, V8Sink> {
 public:
  explicit V8Sink(LegacyV8CpuProfileTracker* tracker) : tracker_(tracker) {}
  void Parse(int64_t ts, LegacyV8CpuProfileEvent data) {
    tracker_->Parse(ts, data);
  }

 private:
  LegacyV8CpuProfileTracker* tracker_;
};

}  // namespace

TrackEventTokenizer::TrackEventTokenizer(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context,
    TrackEventTracker* track_event_tracker)
    : context_(context),
      track_event_tracker_(track_event_tracker),
      module_context_(module_context),
      v8_tracker_(std::make_unique<LegacyV8CpuProfileTracker>(context)),
      v8_stream_(context->sorter->CreateStream(
          std::make_unique<V8Sink>(v8_tracker_.get()))),
      counter_name_thread_time_id_(
          context_->storage->InternString("thread_time")),
      counter_name_thread_instruction_count_id_(
          context_->storage->InternString("thread_instruction_count")),
      counter_unit_ids_{{kNullStringId, context_->storage->InternString("ns"),
                         context_->storage->InternString("count"),
                         context_->storage->InternString("bytes")}} {
  base::ignore_result(module_context_);
}

ModuleResult TrackEventTokenizer::TokenizeRangeOfInterestPacket(
    RefPtr<PacketSequenceStateGeneration> /*state*/,
    const protos::pbzero::TracePacket::Decoder& packet,
    int64_t /*packet_timestamp*/) {
  protos::pbzero::TrackEventRangeOfInterest::Decoder range_of_interest(
      packet.track_event_range_of_interest());
  if (!range_of_interest.has_start_us()) {
    context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
    return ModuleResult::Handled();
  }
  track_event_tracker_->set_range_of_interest_us(range_of_interest.start_us());
  context_->metadata_tracker->SetMetadata(
      metadata::range_of_interest_start_us,
      Variadic::Integer(range_of_interest.start_us()));
  return ModuleResult::Handled();
}

ModuleResult TrackEventTokenizer::TokenizeTrackDescriptorPacket(
    RefPtr<PacketSequenceStateGeneration> state,
    const protos::pbzero::TracePacket::Decoder& packet,
    int64_t packet_timestamp) {
  using TrackDescriptorProto = protos::pbzero::TrackDescriptor;
  using Reservation = TrackEventTracker::DescriptorTrackReservation;
  auto track_descriptor_field = packet.track_descriptor();
  TrackDescriptorProto::Decoder track(track_descriptor_field.data,
                                      track_descriptor_field.size);

  Reservation reservation;

  if (!track.has_uuid()) {
    PERFETTO_ELOG("TrackDescriptor packet without uuid");
    context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
    return ModuleResult::Handled();
  }

  if (track.has_parent_uuid()) {
    reservation.parent_uuid = track.parent_uuid();
  }

  if (track.has_child_ordering()) {
    switch (track.child_ordering()) {
      case TrackDescriptorProto::ChildTracksOrdering::UNKNOWN:
        reservation.ordering = Reservation::ChildTracksOrdering::kUnknown;
        break;
      case TrackDescriptorProto::ChildTracksOrdering::CHRONOLOGICAL:
        reservation.ordering = Reservation::ChildTracksOrdering::kChronological;
        break;
      case TrackDescriptorProto::ChildTracksOrdering::LEXICOGRAPHIC:
        reservation.ordering = Reservation::ChildTracksOrdering::kLexicographic;
        break;
      case TrackDescriptorProto::ChildTracksOrdering::EXPLICIT:
        reservation.ordering = Reservation::ChildTracksOrdering::kExplicit;
        break;
      default:
        context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
        return ModuleResult::Handled();
    }
  }

  if (track.has_sibling_order_rank()) {
    reservation.sibling_order_rank = track.sibling_order_rank();
  }

  if (track.has_sibling_merge_behavior()) {
    using B = TrackDescriptorProto::SiblingMergeBehavior;
    switch (track.sibling_merge_behavior()) {
      case B::SIBLING_MERGE_BEHAVIOR_UNSPECIFIED:
      case B::SIBLING_MERGE_BEHAVIOR_BY_TRACK_NAME:
        reservation.sibling_merge_behavior =
            Reservation::SiblingMergeBehavior::kByName;
        break;
      case B::SIBLING_MERGE_BEHAVIOR_NONE:
        reservation.sibling_merge_behavior =
            Reservation::SiblingMergeBehavior::kNone;
        break;
      case B::SIBLING_MERGE_BEHAVIOR_BY_SIBLING_MERGE_KEY:
        reservation.sibling_merge_behavior =
            Reservation::SiblingMergeBehavior::kByKey;
        if (track.has_sibling_merge_key()) {
          reservation.sibling_merge_key =
              context_->storage->InternString(track.sibling_merge_key());
        } else if (track.has_sibling_merge_key_int()) {
          reservation.sibling_merge_key = context_->storage->InternString(
              "sibling_merge_key_int:" +
              std::to_string(track.sibling_merge_key_int()));
        }
        break;
    }
  }

  if (track.has_name()) {
    reservation.name = context_->storage->InternString(track.name());
  } else if (track.has_static_name()) {
    reservation.name = context_->storage->InternString(track.static_name());
  } else if (track.has_atrace_name()) {
    reservation.name = context_->storage->InternString(track.atrace_name());
  }

  if (track.has_description()) {
    reservation.description =
        context_->storage->InternString(track.description());
  }

  if (packet.has_trusted_pid()) {
    context_->process_tracker->UpdateTrustedPid(
        static_cast<uint32_t>(packet.trusted_pid()), track.uuid());
  }

  if (track.has_thread()) {
    protos::pbzero::ThreadDescriptor::Decoder thread(track.thread());

    if (!thread.has_pid() || !thread.has_tid()) {
      PERFETTO_ELOG(
          "No pid or tid in ThreadDescriptor for track with uuid %" PRIu64,
          track.uuid());
      context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
      return ModuleResult::Handled();
    }

    reservation.min_timestamp = packet_timestamp;
    reservation.pid = static_cast<int64_t>(thread.pid());
    reservation.tid = static_cast<int64_t>(thread.tid());
    reservation.use_separate_track =
        track.disallow_merging_with_system_tracks();

    // If tid is sandboxed then use a unique synthetic tid, to avoid
    // having concurrent threads with the same tid.
    if (track.has_chrome_thread()) {
      protos::pbzero::ChromeThreadDescriptor::Decoder chrome_thread(
          track.chrome_thread());
      if (chrome_thread.has_is_sandboxed_tid()) {
        reservation.use_synthetic_tid = chrome_thread.is_sandboxed_tid();
      }
    }
    track_event_tracker_->ReserveDescriptorTrack(track.uuid(), reservation);

    if (state->IsIncrementalStateValid()) {
      TokenizeThreadDescriptor(*state, thread, reservation.use_synthetic_tid);
    }

    return ModuleResult::Ignored();
  }

  if (track.has_process()) {
    protos::pbzero::ProcessDescriptor::Decoder process(track.process());

    if (!process.has_pid()) {
      PERFETTO_ELOG("No pid in ProcessDescriptor for track with uuid %" PRIu64,
                    track.uuid());
      context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
      return ModuleResult::Handled();
    }

    reservation.pid = static_cast<uint32_t>(process.pid());
    reservation.min_timestamp = packet_timestamp;
    track_event_tracker_->ReserveDescriptorTrack(track.uuid(), reservation);

    return ModuleResult::Ignored();
  }
  if (track.has_counter()) {
    protos::pbzero::CounterDescriptor::Decoder counter(track.counter());

    StringId category_id = kNullStringId;
    if (counter.has_categories()) {
      // TODO(eseckler): Support multi-category events in the table schema.
      std::string categories;
      for (auto it = counter.categories(); it; ++it) {
        if (!categories.empty())
          categories += ",";
        categories.append((*it).data, (*it).size);
      }
      if (!categories.empty()) {
        category_id =
            context_->storage->InternString(base::StringView(categories));
      }
    }

    // TODO(eseckler): Intern counter tracks for specific counter types like
    // thread time, so that the same counter can be referred to from tracks with
    // different uuids. (Chrome may emit thread time values on behalf of other
    // threads, in which case it has to use absolute values on a different
    // track_uuid. Right now these absolute values are imported onto a separate
    // counter track than the other thread's regular thread time values.)
    if (reservation.name.is_null()) {
      switch (counter.type()) {
        case CounterDescriptor::COUNTER_UNSPECIFIED:
          break;
        case CounterDescriptor::COUNTER_THREAD_TIME_NS:
          reservation.name = counter_name_thread_time_id_;
          break;
        case CounterDescriptor::COUNTER_THREAD_INSTRUCTION_COUNT:
          reservation.name = counter_name_thread_instruction_count_id_;
          break;
      }
    }

    reservation.is_counter = true;
    reservation.counter_details =
        TrackEventTracker::DescriptorTrackReservation::CounterDetails{};

    auto& counter_details = *reservation.counter_details;
    counter_details.category = category_id;
    counter_details.is_incremental = counter.is_incremental();
    counter_details.unit_multiplier = counter.unit_multiplier();

    if (counter.has_y_axis_share_key()) {
      counter_details.y_axis_share_key =
          context_->storage->InternString(counter.y_axis_share_key());
    }

    auto unit = static_cast<uint32_t>(counter.unit());
    if (counter.type() == CounterDescriptor::COUNTER_THREAD_TIME_NS) {
      counter_details.unit = counter_unit_ids_[CounterDescriptor::UNIT_TIME_NS];
      counter_details.builtin_type_str = counter_name_thread_time_id_;
    } else if (counter.type() ==
               CounterDescriptor::COUNTER_THREAD_INSTRUCTION_COUNT) {
      counter_details.unit = counter_unit_ids_[CounterDescriptor::UNIT_COUNT];
      counter_details.builtin_type_str =
          counter_name_thread_instruction_count_id_;
    } else if (unit < counter_unit_ids_.size() &&
               unit != CounterDescriptor::COUNTER_UNSPECIFIED) {
      counter_details.unit = counter_unit_ids_[unit];
    } else {
      counter_details.unit =
          context_->storage->InternString(counter.unit_name());
    }

    // Incrementally encoded counters are only valid on a single sequence.
    track_event_tracker_->ReserveDescriptorTrack(track.uuid(), reservation);

    return ModuleResult::Ignored();
  }

  track_event_tracker_->ReserveDescriptorTrack(track.uuid(), reservation);

  // Let ProtoTraceReader forward the packet to the parser.
  return ModuleResult::Ignored();
}  // namespace perfetto::trace_processor

ModuleResult TrackEventTokenizer::TokenizeThreadDescriptorPacket(
    RefPtr<PacketSequenceStateGeneration> state,
    const protos::pbzero::TracePacket::Decoder& packet) {
  if (PERFETTO_UNLIKELY(!packet.has_trusted_packet_sequence_id())) {
    PERFETTO_ELOG("ThreadDescriptor packet without trusted_packet_sequence_id");
    context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
    return ModuleResult::Handled();
  }

  // TrackEvents will be ignored while incremental state is invalid. As a
  // consequence, we should also ignore any ThreadDescriptors received in this
  // state. Otherwise, any delta-encoded timestamps would be calculated
  // incorrectly once we move out of the packet loss state. Instead, wait until
  // the first subsequent descriptor after incremental state is cleared.
  if (!state->IsIncrementalStateValid()) {
    context_->storage->IncrementStats(stats::tokenizer_skipped_packets);
    return ModuleResult::Handled();
  }

  protos::pbzero::ThreadDescriptor::Decoder thread(packet.thread_descriptor());
  TokenizeThreadDescriptor(*state, thread, /*use_synthetic_tid=*/false);

  // Let ProtoTraceReader forward the packet to the parser.
  return ModuleResult::Ignored();
}

void TrackEventTokenizer::TokenizeThreadDescriptor(
    PacketSequenceStateGeneration& state,
    const protos::pbzero::ThreadDescriptor::Decoder& thread,
    bool use_synthetic_tid) {
  // TODO(eseckler): Remove support for legacy thread descriptor-based default
  // tracks and delta timestamps.
  state.SetThreadDescriptor(thread, use_synthetic_tid);
}

ModuleResult TrackEventTokenizer::TokenizeTrackEventPacket(
    RefPtr<PacketSequenceStateGeneration> state,
    const protos::pbzero::TracePacket::Decoder& packet,
    TraceBlobView* packet_blob,
    int64_t packet_timestamp) {
  if (PERFETTO_UNLIKELY(!packet.has_trusted_packet_sequence_id())) {
    PERFETTO_ELOG("TrackEvent packet without trusted_packet_sequence_id");
    context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
    return ModuleResult::Handled();
  }

  protos::pbzero::TrackEvent::Decoder event(packet.track_event());
  protos::pbzero::TrackEventDefaults::Decoder* defaults =
      state->GetTrackEventDefaults();

  int64_t timestamp;
  TrackEventData data(std::move(*packet_blob), state);

  // TODO(eseckler): Remove handling of timestamps relative to ThreadDescriptors
  // once all producers have switched to clock-domain timestamps (e.g.
  // TracePacket's timestamp).

  if (event.has_timestamp_delta_us()) {
    // Delta timestamps require a valid ThreadDescriptor packet since the last
    // packet loss.
    if (!state->track_event_timestamps_valid()) {
      context_->storage->IncrementStats(stats::tokenizer_skipped_packets);
      return ModuleResult::Handled();
    }
    timestamp = state->IncrementAndGetTrackEventTimeNs(
        event.timestamp_delta_us() * 1000);

    // Legacy TrackEvent timestamp fields are in MONOTONIC domain. Adjust to
    // trace time if we have a clock snapshot.
    base::StatusOr<int64_t> trace_ts = context_->clock_tracker->ToTraceTime(
        protos::pbzero::BUILTIN_CLOCK_MONOTONIC, timestamp);
    if (trace_ts.ok())
      timestamp = trace_ts.value();
  } else if (int64_t ts_absolute_us = event.timestamp_absolute_us()) {
    // One-off absolute timestamps don't affect delta computation.
    timestamp = ts_absolute_us * 1000;

    // Legacy TrackEvent timestamp fields are in MONOTONIC domain. Adjust to
    // trace time if we have a clock snapshot.
    base::StatusOr<int64_t> trace_ts = context_->clock_tracker->ToTraceTime(
        protos::pbzero::BUILTIN_CLOCK_MONOTONIC, timestamp);
    if (trace_ts.ok())
      timestamp = trace_ts.value();
  } else if (packet.has_timestamp()) {
    timestamp = packet_timestamp;
  } else {
    PERFETTO_ELOG("TrackEvent without valid timestamp");
    context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
    return ModuleResult::Handled();
  }

  // Handle legacy sample events which might have timestamps embedded inside.
  if (PERFETTO_UNLIKELY(event.has_legacy_event())) {
    protos::pbzero::TrackEvent::LegacyEvent::Decoder leg(event.legacy_event());
    if (PERFETTO_UNLIKELY(leg.phase() == 'P')) {
      base::Status status = TokenizeLegacySampleEvent(
          event, leg, *data.trace_packet_data.sequence_state);
      if (!status.ok()) {
        context_->storage->IncrementStats(
            stats::legacy_v8_cpu_profile_invalid_sample);
      }
    }
  }

  if (event.has_thread_time_delta_us()) {
    // Delta timestamps require a valid ThreadDescriptor packet since the last
    // packet loss.
    if (!state->track_event_timestamps_valid()) {
      context_->storage->IncrementStats(stats::tokenizer_skipped_packets);
      return ModuleResult::Handled();
    }
    data.thread_timestamp = state->IncrementAndGetTrackEventThreadTimeNs(
        event.thread_time_delta_us() * 1000);
  } else if (event.has_thread_time_absolute_us()) {
    // One-off absolute timestamps don't affect delta computation.
    data.thread_timestamp = event.thread_time_absolute_us() * 1000;
  }

  if (event.has_thread_instruction_count_delta()) {
    // Delta timestamps require a valid ThreadDescriptor packet since the last
    // packet loss.
    if (!state->track_event_timestamps_valid()) {
      context_->storage->IncrementStats(stats::tokenizer_skipped_packets);
      return ModuleResult::Handled();
    }
    data.thread_instruction_count =
        state->IncrementAndGetTrackEventThreadInstructionCount(
            event.thread_instruction_count_delta());
  } else if (event.has_thread_instruction_count_absolute()) {
    // One-off absolute timestamps don't affect delta computation.
    data.thread_instruction_count = event.thread_instruction_count_absolute();
  }

  if (event.type() == protos::pbzero::TrackEvent::TYPE_COUNTER) {
    // Consider track_uuid from the packet and TrackEventDefaults.
    uint64_t track_uuid;
    if (event.has_track_uuid()) {
      track_uuid = event.track_uuid();
    } else if (defaults && defaults->has_track_uuid()) {
      track_uuid = defaults->track_uuid();
    } else {
      PERFETTO_DLOG(
          "Ignoring TrackEvent with counter_value but without track_uuid");
      context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
      return ModuleResult::Handled();
    }

    if (!event.has_counter_value() && !event.has_double_counter_value()) {
      PERFETTO_DLOG(
          "Ignoring TrackEvent with TYPE_COUNTER but without counter_value or "
          "double_counter_value for "
          "track_uuid %" PRIu64,
          track_uuid);
      context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
      return ModuleResult::Handled();
    }

    std::optional<double> value;
    if (event.has_counter_value()) {
      value = track_event_tracker_->ConvertToAbsoluteCounterValue(
          state.get(), track_uuid, static_cast<double>(event.counter_value()));
    } else {
      value = track_event_tracker_->ConvertToAbsoluteCounterValue(
          state.get(), track_uuid, event.double_counter_value());
    }

    if (!value) {
      PERFETTO_DLOG("Ignoring TrackEvent with invalid track_uuid %" PRIu64,
                    track_uuid);
      context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
      return ModuleResult::Handled();
    }

    data.counter_value = *value;
  }

  size_t index = 0;
  const protozero::RepeatedFieldIterator<uint64_t> kEmptyIterator;
  auto result = AddExtraCounterValues(
      *state, data, index, event.extra_counter_values(),
      event.extra_counter_track_uuids(),
      defaults ? defaults->extra_counter_track_uuids() : kEmptyIterator);
  if (!result.ok()) {
    PERFETTO_DLOG("%s", result.c_message());
    context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
    return ModuleResult::Handled();
  }
  result = AddExtraCounterValues(
      *state, data, index, event.extra_double_counter_values(),
      event.extra_double_counter_track_uuids(),
      defaults ? defaults->extra_double_counter_track_uuids() : kEmptyIterator);
  if (!result.ok()) {
    PERFETTO_DLOG("%s", result.c_message());
    context_->storage->IncrementStats(stats::track_event_tokenizer_errors);
    return ModuleResult::Handled();
  }
  module_context_->track_event_stream->Push(timestamp, std::move(data));
  return ModuleResult::Handled();
}

template <typename T>
base::Status TrackEventTokenizer::AddExtraCounterValues(
    PacketSequenceStateGeneration& state,
    TrackEventData& data,
    size_t& index,
    protozero::RepeatedFieldIterator<T> value_it,
    protozero::RepeatedFieldIterator<uint64_t> packet_track_uuid_it,
    protozero::RepeatedFieldIterator<uint64_t> default_track_uuid_it) {
  if (!value_it)
    return base::OkStatus();

  // Consider extra_{double_,}counter_track_uuids from the packet and
  // TrackEventDefaults.
  protozero::RepeatedFieldIterator<uint64_t> track_uuid_it;
  if (packet_track_uuid_it) {
    track_uuid_it = packet_track_uuid_it;
  } else if (default_track_uuid_it) {
    track_uuid_it = default_track_uuid_it;
  } else {
    return base::ErrStatus(
        "Ignoring TrackEvent with extra_{double_,}counter_values but without "
        "extra_{double_,}counter_track_uuids");
  }

  for (; value_it; ++value_it, ++track_uuid_it, ++index) {
    if (!*track_uuid_it) {
      return base::ErrStatus(
          "Ignoring TrackEvent with more extra_{double_,}counter_values than "
          "extra_{double_,}counter_track_uuids");
    }
    if (index >= TrackEventData::kMaxNumExtraCounters) {
      return base::ErrStatus(
          "Ignoring TrackEvent with more extra_{double_,}counter_values than "
          "TrackEventData::kMaxNumExtraCounters");
    }
    std::optional<double> abs_value =
        track_event_tracker_->ConvertToAbsoluteCounterValue(
            &state, *track_uuid_it, static_cast<double>(*value_it));
    if (!abs_value) {
      return base::ErrStatus(
          "Ignoring TrackEvent with invalid extra counter track");
    }
    data.extra_counter_values[index] = *abs_value;
  }
  return base::OkStatus();
}

base::Status TrackEventTokenizer::TokenizeLegacySampleEvent(
    const protos::pbzero::TrackEvent::Decoder& event,
    const protos::pbzero::TrackEvent::LegacyEvent::Decoder& legacy,
    PacketSequenceStateGeneration& state) {
#if PERFETTO_BUILDFLAG(PERFETTO_TP_JSON)
  for (auto it = event.debug_annotations(); it; ++it) {
    protos::pbzero::DebugAnnotation::Decoder da(*it);
    auto* interned_name = state.LookupInternedMessage<
        protos::pbzero::InternedData::kDebugAnnotationNamesFieldNumber,
        protos::pbzero::DebugAnnotationName>(da.name_iid());
    base::StringView name(interned_name->name());
    if (name != "data" || !da.has_legacy_json_value()) {
      continue;
    }
    auto opt_val = json::ParseJsonString(da.legacy_json_value());
    if (!opt_val) {
      continue;
    }
    const auto& val = *opt_val;
    if (val.isMember("startTime")) {
      ASSIGN_OR_RETURN(int64_t ts, context_->clock_tracker->ToTraceTime(
                                       protos::pbzero::BUILTIN_CLOCK_MONOTONIC,
                                       val["startTime"].asInt64() * 1000));
      v8_tracker_->SetStartTsForSessionAndPid(
          legacy.unscoped_id(), static_cast<uint32_t>(state.pid()), ts);
      continue;
    }
    const auto& profile = val["cpuProfile"];
    for (const auto& n : profile["nodes"]) {
      uint32_t node_id = n["id"].asUInt();
      std::optional<uint32_t> parent_node_id =
          n.isMember("parent") ? std::make_optional(n["parent"].asUInt())
                               : std::nullopt;
      const auto& frame = n["callFrame"];
      base::StringView url =
          frame.isMember("url") ? frame["url"].asCString() : base::StringView();
      base::StringView function_name = frame["functionName"].asCString();
      base::Status status = v8_tracker_->AddCallsite(
          legacy.unscoped_id(), static_cast<uint32_t>(state.pid()), node_id,
          parent_node_id, url, function_name, {});
      if (!status.ok()) {
        context_->storage->IncrementStats(
            stats::legacy_v8_cpu_profile_invalid_callsite);
        continue;
      }
    }
    const auto& samples = profile["samples"];
    const auto& deltas = val["timeDeltas"];
    if (samples.size() != deltas.size()) {
      return base::ErrStatus(
          "v8 legacy profile: samples and timestamps do not have same size");
    }
    for (uint32_t i = 0; i < samples.size(); ++i) {
      ASSIGN_OR_RETURN(int64_t ts, v8_tracker_->AddDeltaAndGetTs(
                                       legacy.unscoped_id(),
                                       static_cast<uint32_t>(state.pid()),
                                       deltas[i].asInt64() * 1000));
      v8_stream_->Push(
          ts, {legacy.unscoped_id(), static_cast<uint32_t>(state.pid()),
               static_cast<uint32_t>(state.tid()), samples[i].asUInt()});
    }
  }
#else
  base::ignore_result(event, legacy, state);
#endif
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
