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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_EVENT_IMPORTER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_EVENT_IMPORTER_H_

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/fixed_string_writer.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/public/compiler.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/flow_tracker.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/synthetic_tid.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/common/tracks_internal.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/importers/proto/packet_analyzer.h"
#include "src/trace_processor/importers/proto/stack_profile_sequence_state.h"
#include "src/trace_processor/importers/proto/track_event_parser.h"
#include "src/trace_processor/importers/proto/track_event_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/debug_annotation_parser.h"
#include "src/trace_processor/util/proto_to_args_parser.h"

#include "protos/perfetto/common/android_log_constants.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/track_event/chrome_active_processes.pbzero.h"
#include "protos/perfetto/trace/track_event/chrome_compositor_scheduler_state.pbzero.h"
#include "protos/perfetto/trace/track_event/chrome_histogram_sample.pbzero.h"
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"
#include "protos/perfetto/trace/track_event/log_message.pbzero.h"
#include "protos/perfetto/trace/track_event/source_location.pbzero.h"
#include "protos/perfetto/trace/track_event/task_execution.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"

namespace Json {
class Value;
}

namespace perfetto::trace_processor {

using BoundInserter = ArgsTracker::BoundInserter;
using protos::pbzero::TrackEvent;
using LegacyEvent = TrackEvent::LegacyEvent;
using protozero::ConstBytes;

// Slices which have been opened but haven't been closed yet will be marked
// with these placeholder values.
constexpr int64_t kPendingThreadDuration = -1;
constexpr int64_t kPendingThreadInstructionDelta = -1;

// Paths on Windows use backslash rather than slash as a separator.
// Normalise the paths by replacing backslashes with slashes to make it
// easier to write cross-platform scripts.
inline std::string NormalizePathSeparators(const protozero::ConstChars& path) {
  std::string result(path.data, path.size);
  for (char& c : result) {
    if (c == '\\')
      c = '/';
  }
  return result;
}

inline TrackCompressor::AsyncSliceType AsyncSliceTypeForPhase(int32_t phase) {
  switch (phase) {
    case 'b':
    case 'S':
      return TrackCompressor::AsyncSliceType::kBegin;
    case 'e':
    case 'T':
      return TrackCompressor::AsyncSliceType::kEnd;
    case 'n':
    case 'p':
    case 'F':
      return TrackCompressor::AsyncSliceType::kInstant;
  }
  PERFETTO_FATAL("For GCC");
}

inline protos::pbzero::AndroidLogPriority ToAndroidLogPriority(
    protos::pbzero::LogMessage::Priority prio) {
  switch (prio) {
    case protos::pbzero::LogMessage::Priority::PRIO_UNSPECIFIED:
      return protos::pbzero::AndroidLogPriority::PRIO_UNSPECIFIED;
    case protos::pbzero::LogMessage::Priority::PRIO_UNUSED:
      return protos::pbzero::AndroidLogPriority::PRIO_UNUSED;
    case protos::pbzero::LogMessage::Priority::PRIO_VERBOSE:
      return protos::pbzero::AndroidLogPriority::PRIO_VERBOSE;
    case protos::pbzero::LogMessage::Priority::PRIO_DEBUG:
      return protos::pbzero::AndroidLogPriority::PRIO_DEBUG;
    case protos::pbzero::LogMessage::Priority::PRIO_INFO:
      return protos::pbzero::AndroidLogPriority::PRIO_INFO;
    case protos::pbzero::LogMessage::Priority::PRIO_WARN:
      return protos::pbzero::AndroidLogPriority::PRIO_WARN;
    case protos::pbzero::LogMessage::Priority::PRIO_ERROR:
      return protos::pbzero::AndroidLogPriority::PRIO_ERROR;
    case protos::pbzero::LogMessage::Priority::PRIO_FATAL:
      return protos::pbzero::AndroidLogPriority::PRIO_FATAL;
  }
  return protos::pbzero::AndroidLogPriority::PRIO_UNSPECIFIED;
}

class TrackEventEventImporter {
 public:
  TrackEventEventImporter(TrackEventParser* parser,
                          int64_t ts,
                          const TrackEventData* event_data,
                          protozero::ConstBytes blob,
                          uint32_t packet_sequence_id)
      : context_(parser->context_),
        track_event_tracker_(parser->track_event_tracker_),
        storage_(context_->storage.get()),
        parser_(parser),
        args_translation_table_(context_->args_translation_table.get()),
        ts_(ts),
        event_data_(event_data),
        sequence_state_(event_data->trace_packet_data.sequence_state.get()),
        blob_(blob),
        event_(blob_),
        legacy_event_(event_.legacy_event()),
        defaults_(event_data->trace_packet_data.sequence_state
                      ->GetTrackEventDefaults()),
        thread_timestamp_(event_data->thread_timestamp),
        thread_instruction_count_(event_data->thread_instruction_count),
        packet_sequence_id_(packet_sequence_id) {}

  base::Status Import() {
    // TODO(eseckler): This legacy event field will eventually be replaced by
    // fields in TrackEvent itself.
    if (PERFETTO_UNLIKELY(!event_.type() && !legacy_event_.has_phase()))
      return base::ErrStatus("TrackEvent without type or phase");

    category_id_ = ParseTrackEventCategory();
    name_id_ = ParseTrackEventName();

    if (context_->content_analyzer) {
      PacketAnalyzer::SampleAnnotation annotation;
      annotation.emplace_back(parser_->event_category_key_id_, category_id_);
      annotation.emplace_back(parser_->event_name_key_id_, name_id_);
      PacketAnalyzer::Get(context_)->ProcessPacket(
          event_data_->trace_packet_data.packet, annotation);
    }

    RETURN_IF_ERROR(ParseInitialTrackAssociation());

    // If we have legacy thread time / instruction count fields, also parse them
    // into the counters tables.
    ParseLegacyThreadTimeAndInstructionsAsCounters();

    // Parse extra counter values before parsing the actual event. This way, we
    // can update the slice's thread time / instruction count fields based on
    // these counter values and also parse them as slice attributes / arguments.
    ParseExtraCounterValues();

    // Non-legacy counters are treated differently. Legacy counters do not have
    // a track_id_ and should instead go through the switch below.
    if (event_.type() == TrackEvent::TYPE_COUNTER) {
      return ParseCounterEvent();
    }

    // TODO(eseckler): Replace phase with type and remove handling of
    // legacy_event_.phase() once it is no longer used by producers.
    char phase = static_cast<char>(ParsePhaseOrType());

    switch (phase) {
      case 'B':  // TRACE_EVENT_PHASE_BEGIN.
        return ParseThreadBeginEvent();
      case 'E':  // TRACE_EVENT_PHASE_END.
        return ParseThreadEndEvent();
      case 'X':  // TRACE_EVENT_PHASE_COMPLETE.
        return ParseThreadCompleteEvent();
      case 's':  // TRACE_EVENT_PHASE_FLOW_BEGIN.
      case 't':  // TRACE_EVENT_PHASE_FLOW_STEP.
      case 'f':  // TRACE_EVENT_PHASE_FLOW_END.
        return ParseFlowEventV1(phase);
      case 'i':
      case 'I':  // TRACE_EVENT_PHASE_INSTANT.
      case 'R':  // TRACE_EVENT_PHASE_MARK.
        return ParseThreadInstantEvent(phase);
      case 'b':  // TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN
      case 'S':
        return ParseAsyncBeginEvent(phase);
      case 'e':  // TRACE_EVENT_PHASE_NESTABLE_ASYNC_END
      case 'F':
        return ParseAsyncEndEvent();
      case 'n':  // TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT
        return ParseAsyncInstantEvent();
      case 'T':
      case 'p':
        return ParseAsyncStepEvent(phase);
      case 'M':  // TRACE_EVENT_PHASE_METADATA (process and thread names).
        return ParseMetadataEvent();
      default:
        // Other events are proxied via the raw table for JSON export.
        return ParseLegacyEventAsRawEvent();
    }
  }

 private:
  StringId ParseTrackEventCategory() {
    StringId category_id = kNullStringId;

    std::vector<uint64_t> category_iids;
    for (auto it = event_.category_iids(); it; ++it) {
      category_iids.push_back(*it);
    }
    std::vector<protozero::ConstChars> category_strings;
    for (auto it = event_.categories(); it; ++it) {
      category_strings.push_back(*it);
    }

    // If there's a single category, we can avoid building a concatenated
    // string.
    if (PERFETTO_LIKELY(category_iids.size() == 1 &&
                        category_strings.empty())) {
      auto* decoder = sequence_state_->LookupInternedMessage<
          protos::pbzero::InternedData::kEventCategoriesFieldNumber,
          protos::pbzero::EventCategory>(category_iids[0]);
      if (decoder) {
        category_id = storage_->InternString(decoder->name());
      } else {
        char buffer[32];
        base::FixedStringWriter writer(buffer, sizeof(buffer));
        writer.AppendLiteral("unknown(");
        writer.AppendUnsignedInt(category_iids[0]);
        writer.AppendChar(')');
        category_id = storage_->InternString(writer.GetStringView());
      }
    } else if (category_iids.empty() && category_strings.size() == 1) {
      category_id = storage_->InternString(category_strings[0]);
    } else if (category_iids.size() + category_strings.size() > 1) {
      // We concatenate the category strings together since we currently only
      // support a single "cat" column.
      // TODO(eseckler): Support multi-category events in the table schema.
      std::string categories;
      for (uint64_t iid : category_iids) {
        auto* decoder = sequence_state_->LookupInternedMessage<
            protos::pbzero::InternedData::kEventCategoriesFieldNumber,
            protos::pbzero::EventCategory>(iid);
        if (!decoder)
          continue;
        base::StringView name = decoder->name();
        if (!categories.empty())
          categories.append(",");
        categories.append(name.data(), name.size());
      }
      for (const protozero::ConstChars& cat : category_strings) {
        if (!categories.empty())
          categories.append(",");
        categories.append(cat.data, cat.size);
      }
      if (!categories.empty())
        category_id = storage_->InternString(base::StringView(categories));
    }

    return category_id;
  }

  StringId ParseTrackEventName() {
    uint64_t name_iid = event_.name_iid();
    if (!name_iid)
      name_iid = legacy_event_.name_iid();

    if (PERFETTO_LIKELY(name_iid)) {
      auto* decoder = sequence_state_->LookupInternedMessage<
          protos::pbzero::InternedData::kEventNamesFieldNumber,
          protos::pbzero::EventName>(name_iid);
      if (decoder)
        return storage_->InternString(decoder->name());
    } else if (event_.has_name()) {
      return storage_->InternString(event_.name());
    }

    return kNullStringId;
  }

  base::Status ParseInitialTrackAssociation() {
    ProcessTracker* procs = context_->process_tracker.get();

    // Consider track_uuid from the packet and TrackEventDefaults, fall back to
    // the default descriptor track (uuid 0).
    track_uuid_ = event_.has_track_uuid()
                      ? event_.track_uuid()
                      : (defaults_ && defaults_->has_track_uuid()
                             ? defaults_->track_uuid()
                             : 0u);

    // Determine track from track_uuid specified in either TrackEvent or
    // TrackEventDefaults. If a non-default track is not set, we either:
    //   a) fall back to the track specified by the sequence's (or event's) pid
    //      + tid (only in case of legacy tracks/events, i.e. events that don't
    //      specify an explicit track uuid or use legacy event phases instead of
    //      TrackEvent types), or
    //   b) a default track.
    if (track_uuid_) {
      auto resolved = track_event_tracker_->ResolveDescriptorTrack(track_uuid_);
      if (!resolved) {
        return base::ErrStatus(
            "track_event_parser: unable to resolve track matching UUID "
            "%" PRIu64,
            track_uuid_);
      }
      switch (resolved->scope()) {
        case TrackEventTracker::ResolvedDescriptorTrack::Scope::kThread:
          utid_ = resolved->utid();
          upid_ = storage_->thread_table()[*utid_].upid();
          break;
        case TrackEventTracker::ResolvedDescriptorTrack::Scope::kProcess:
          upid_ = resolved->upid();
          // TODO: b/175152326 - Should pid namespace translation also be done
          // here?
          if (sequence_state_->pid_and_tid_valid()) {
            auto pid = static_cast<uint32_t>(sequence_state_->pid());
            auto tid = static_cast<uint32_t>(sequence_state_->tid());
            UniqueTid utid_candidate = procs->UpdateThread(tid, pid);
            if (storage_->thread_table()[utid_candidate].upid() == upid_) {
              legacy_passthrough_utid_ = utid_candidate;
            }
          }
          break;
        case TrackEventTracker::ResolvedDescriptorTrack::Scope::kGlobal:
          // TODO: b/175152326 - Should pid namespace translation also be done
          // here?
          if (sequence_state_->pid_and_tid_valid()) {
            auto pid = static_cast<uint32_t>(sequence_state_->pid());
            auto tid = static_cast<uint32_t>(sequence_state_->tid());
            legacy_passthrough_utid_ = procs->UpdateThread(tid, pid);
          }
          break;
      }
    } else {
      bool pid_tid_state_valid = sequence_state_->pid_and_tid_valid();

      // We have a 0-value |track_uuid|. Nevertheless, we should only fall back
      // if we have either no |track_uuid| specified at all or |track_uuid| was
      // set explicitly to 0 (e.g. to override a default track_uuid) and we have
      // a legacy phase. Events with real phases should use |track_uuid| to
      // specify a different track (or use the pid/tid_override fields).
      fallback_to_legacy_pid_tid_tracks_ =
          (!event_.has_track_uuid() || !event_.has_type()) &&
          pid_tid_state_valid;

      // Always allow fallback if we have a process override.
      fallback_to_legacy_pid_tid_tracks_ |= legacy_event_.has_pid_override();

      // A thread override requires a valid pid.
      fallback_to_legacy_pid_tid_tracks_ |=
          legacy_event_.has_tid_override() && pid_tid_state_valid;

      if (fallback_to_legacy_pid_tid_tracks_) {
        // TODO: b/175152326 - Should pid namespace translation also be done
        // here?
        auto pid = static_cast<uint32_t>(sequence_state_->pid());
        auto tid = sequence_state_->tid();
        if (legacy_event_.has_pid_override()) {
          pid = static_cast<uint32_t>(legacy_event_.pid_override());
          // Create a synthetic tid while avoiding using the exact same tid in
          // different processes.
          tid = CreateSyntheticTid(-1, pid);
        }
        if (legacy_event_.has_tid_override()) {
          tid = static_cast<uint32_t>(legacy_event_.tid_override());
          if (IsSyntheticTid(sequence_state_->tid())) {
            tid = CreateSyntheticTid(tid, pid);
          }
        }

        if (!pid || !tid) {
          return base::ErrStatus(
              "track_event_parser: pid/tid 0 is reserved for swapper thread");
        }

        utid_ = procs->UpdateThread(tid, pid);
        upid_ = storage_->thread_table()[*utid_].upid();
      }
    }

    if (!legacy_event_.has_phase())
      return base::OkStatus();

    // Legacy phases may imply a different track than the one specified by
    // the fallback (or default track uuid) above.
    switch (legacy_event_.phase()) {
      case 'b':
      case 'e':
      case 'n':
      case 'S':
      case 'T':
      case 'p':
      case 'F': {
        legacy_passthrough_utid_ = utid_;
        break;
      }
      case 'i':
      case 'I': {
        // Intern tracks for global or process-scoped legacy instant events.
        switch (legacy_event_.instant_event_scope()) {
          case LegacyEvent::SCOPE_UNSPECIFIED:
          case LegacyEvent::SCOPE_THREAD:
            // Thread-scoped legacy instant events already have the right
            // track based on the tid/pid of the sequence.
            if (!utid_) {
              return base::ErrStatus(
                  "Thread-scoped instant event without thread association");
            }
            break;
          case LegacyEvent::SCOPE_GLOBAL:
            legacy_passthrough_utid_ = utid_;
            utid_ = std::nullopt;
            break;
          case LegacyEvent::SCOPE_PROCESS:
            if (!upid_) {
              return base::ErrStatus(
                  "Process-scoped instant event without process association");
            }
            legacy_passthrough_utid_ = utid_;
            utid_ = std::nullopt;
            break;
        }
        break;
      }
      default:
        break;
    }
    return base::OkStatus();
  }

  base::StatusOr<TrackId> ParseTrackAssociationBegin() {
    if (!track_uuid_ && fallback_to_legacy_pid_tid_tracks_) {
      return ParseTrackAssociationInternal(std::nullopt);
    }
    return ParseTrackAssociationInternal(
        track_event_tracker_->InternDescriptorTrackBegin(
            track_uuid_, name_id_,
            track_uuid_ ? std::make_optional(packet_sequence_id_)
                        : std::nullopt));
  }

  base::StatusOr<TrackId> ParseTrackAssociationEnd() {
    if (!track_uuid_ && fallback_to_legacy_pid_tid_tracks_) {
      return ParseTrackAssociationInternal(std::nullopt);
    }
    return ParseTrackAssociationInternal(
        track_event_tracker_->InternDescriptorTrackEnd(
            track_uuid_, name_id_,
            track_uuid_ ? std::make_optional(packet_sequence_id_)
                        : std::nullopt));
  }

  base::StatusOr<TrackId> ParseTrackAssociationInstant() {
    if (!track_uuid_ && fallback_to_legacy_pid_tid_tracks_) {
      return ParseTrackAssociationInternal(std::nullopt);
    }
    return ParseTrackAssociationInternal(
        track_event_tracker_->InternDescriptorTrackInstant(
            track_uuid_, name_id_,
            track_uuid_ ? std::make_optional(packet_sequence_id_)
                        : std::nullopt));
  }

  base::StatusOr<TrackId> ParseTrackAssociationCounter() {
    if (!track_uuid_ && fallback_to_legacy_pid_tid_tracks_) {
      return ParseTrackAssociationInternal(std::nullopt);
    }
    return ParseTrackAssociationInternal(
        track_event_tracker_->InternDescriptorTrackCounter(
            track_uuid_, name_id_,
            track_uuid_ ? std::make_optional(packet_sequence_id_)
                        : std::nullopt));
  }

  base::StatusOr<TrackId> ParseTrackAssociationForLegacy() {
    if (!track_uuid_ && fallback_to_legacy_pid_tid_tracks_) {
      return ParseTrackAssociationInternal(std::nullopt);
    }
    return ParseTrackAssociationInternal(
        track_event_tracker_->InternDescriptorTrackLegacy(
            track_uuid_, name_id_,
            track_uuid_ ? std::make_optional(packet_sequence_id_)
                        : std::nullopt));
  }

  base::StatusOr<TrackId> ParseTrackAssociationInternal(
      std::optional<TrackId> opt_id) {
    TrackTracker* track_tracker = context_->track_tracker.get();

    // Legacy phases may imply a different track than the one specified by
    // the fallback (or default track uuid) above.
    switch (legacy_event_.phase()) {
      case 'b':
      case 'e':
      case 'n':
      case 'S':
      case 'T':
      case 'p':
      case 'F': {
        // Intern tracks for legacy async events based on legacy event ids.
        int64_t source_id = 0;
        bool source_id_is_process_scoped = false;
        if (legacy_event_.has_unscoped_id()) {
          source_id = static_cast<int64_t>(legacy_event_.unscoped_id());
        } else if (legacy_event_.has_global_id()) {
          source_id = static_cast<int64_t>(legacy_event_.global_id());
        } else if (legacy_event_.has_local_id()) {
          if (!upid_) {
            return base::ErrStatus(
                "TrackEvent with local_id without process association");
          }
          source_id = static_cast<int64_t>(legacy_event_.local_id());
          source_id_is_process_scoped = true;
        } else {
          return base::ErrStatus("Async LegacyEvent without ID");
        }
        legacy_trace_source_id_ = source_id;

        // Catapult treats nestable async events of different categories with
        // the same ID as separate tracks. We replicate the same behavior
        // here. For legacy async events, it uses different tracks based on
        // event names.
        const bool legacy_async =
            legacy_event_.phase() == 'S' || legacy_event_.phase() == 'T' ||
            legacy_event_.phase() == 'p' || legacy_event_.phase() == 'F';
        StringId id_scope = legacy_async ? name_id_ : category_id_;
        if (legacy_event_.has_id_scope()) {
          std::string concat = storage_->GetString(category_id_).ToStdString() +
                               ":" + legacy_event_.id_scope().ToStdString();
          id_scope = storage_->InternString(base::StringView(concat));
        }
        return context_->track_compressor->InternLegacyAsyncTrack(
            name_id_, upid_.value_or(0), source_id, source_id_is_process_scoped,
            id_scope, AsyncSliceTypeForPhase(legacy_event_.phase()));
      }
      case 'i':
      case 'I': {
        // Intern tracks for global or process-scoped legacy instant events.
        switch (legacy_event_.instant_event_scope()) {
          case LegacyEvent::SCOPE_UNSPECIFIED:
          case LegacyEvent::SCOPE_THREAD:
            // Thread-scoped legacy instant events already have the right
            // track based on the tid/pid of the sequence.
            break;
          case LegacyEvent::SCOPE_GLOBAL:
            return context_->track_tracker->InternTrack(
                tracks::kLegacyGlobalInstantsBlueprint, tracks::Dimensions(),
                tracks::BlueprintName(),
                [this](ArgsTracker::BoundInserter& inserter) {
                  inserter.AddArg(
                      context_->storage->InternString("source"),
                      Variadic::String(
                          context_->storage->InternString("chrome")));
                });
          case LegacyEvent::SCOPE_PROCESS:
            return context_->track_tracker->InternTrack(
                tracks::kChromeProcessInstantBlueprint,
                tracks::Dimensions(*upid_), tracks::BlueprintName(),
                [this](ArgsTracker::BoundInserter& inserter) {
                  inserter.AddArg(
                      context_->storage->InternString("source"),
                      Variadic::String(
                          context_->storage->InternString("chrome")));
                });
        }
        break;
      }
      default:
        break;
    }

    if (!track_uuid_ && fallback_to_legacy_pid_tid_tracks_) {
      return track_tracker->InternThreadTrack(*utid_);
    }
    if (!opt_id) {
      return base::ErrStatus(
          "track_event_parser: unable to find track matching UUID %" PRIu64,
          track_uuid_);
    }
    return *opt_id;
  }

  int32_t ParsePhaseOrType() {
    if (legacy_event_.has_phase())
      return legacy_event_.phase();

    switch (event_.type()) {
      case TrackEvent::TYPE_SLICE_BEGIN:
        return utid_ ? 'B' : 'b';
      case TrackEvent::TYPE_SLICE_END:
        return utid_ ? 'E' : 'e';
      case TrackEvent::TYPE_INSTANT:
        return utid_ ? 'i' : 'n';
      default:
        PERFETTO_ELOG("unexpected event type %d", event_.type());
        return 0;
    }
  }

  base::Status ParseCounterEvent() {
    // Tokenizer ensures that TYPE_COUNTER events are associated with counter
    // tracks and have values.
    ASSIGN_OR_RETURN(auto track_id, ParseTrackAssociationCounter());
    PERFETTO_DCHECK(storage_->track_table().FindById(track_id));
    PERFETTO_DCHECK(event_.has_counter_value() ||
                    event_.has_double_counter_value());

    context_->event_tracker->PushCounter(
        ts_, static_cast<double>(event_data_->counter_value), track_id,
        [this](BoundInserter* inserter) { ParseTrackEventArgs(inserter); });
    return base::OkStatus();
  }

  void ParseLegacyThreadTimeAndInstructionsAsCounters() {
    if (!utid_)
      return;
    // When these fields are set, we don't expect TrackDescriptor-based counters
    // for thread time or instruction count for this thread in the trace, so we
    // intern separate counter tracks based on name + utid. Note that we cannot
    // import the counter values from the end of a complete event, because the
    // EventTracker expects counters to be pushed in order of their timestamps.
    // One more reason to switch to split begin/end events.
    if (thread_timestamp_) {
      static constexpr auto kBlueprint = tracks::CounterBlueprint(
          "thread_time", tracks::UnknownUnitBlueprint(),
          tracks::DimensionBlueprints(tracks::kThreadDimensionBlueprint),
          tracks::DynamicNameBlueprint());
      TrackId track_id = context_->track_tracker->InternTrack(
          kBlueprint, tracks::Dimensions(*utid_),
          tracks::DynamicName(parser_->counter_name_thread_time_id_));
      context_->event_tracker->PushCounter(
          ts_, static_cast<double>(*thread_timestamp_), track_id);
    }
    if (thread_instruction_count_) {
      static constexpr auto kBlueprint = tracks::CounterBlueprint(
          "thread_instructions", tracks::UnknownUnitBlueprint(),
          tracks::DimensionBlueprints(tracks::kThreadDimensionBlueprint),
          tracks::DynamicNameBlueprint());
      TrackId track_id = context_->track_tracker->InternTrack(
          kBlueprint, tracks::Dimensions(*utid_),
          tracks::DynamicName(
              parser_->counter_name_thread_instruction_count_id_));
      context_->event_tracker->PushCounter(
          ts_, static_cast<double>(*thread_instruction_count_), track_id);
    }
  }

  void ParseExtraCounterValues() {
    if (!event_.has_extra_counter_values() &&
        !event_.has_extra_double_counter_values()) {
      return;
    }

    // Add integer extra counter values.
    size_t index = 0;
    protozero::RepeatedFieldIterator<uint64_t> track_uuid_it;
    if (event_.has_extra_counter_track_uuids()) {
      track_uuid_it = event_.extra_counter_track_uuids();
    } else if (defaults_ && defaults_->has_extra_counter_track_uuids()) {
      track_uuid_it = defaults_->extra_counter_track_uuids();
    }
    for (auto value_it = event_.extra_counter_values(); value_it;
         ++value_it, ++track_uuid_it, ++index) {
      AddExtraCounterValue(track_uuid_it, index);
    }

    // Add double extra counter values.
    track_uuid_it = protozero::RepeatedFieldIterator<uint64_t>();
    if (event_.has_extra_double_counter_track_uuids()) {
      track_uuid_it = event_.extra_double_counter_track_uuids();
    } else if (defaults_ && defaults_->has_extra_double_counter_track_uuids()) {
      track_uuid_it = defaults_->extra_double_counter_track_uuids();
    }
    for (auto value_it = event_.extra_double_counter_values(); value_it;
         ++value_it, ++track_uuid_it, ++index) {
      AddExtraCounterValue(track_uuid_it, index);
    }
  }

  void AddExtraCounterValue(
      protozero::RepeatedFieldIterator<uint64_t> track_uuid_it,
      size_t index) {
    // Tokenizer ensures that there aren't more values than uuids, that we
    // don't have more values than kMaxNumExtraCounters and that the
    // track_uuids are for valid counter tracks.
    PERFETTO_DCHECK(track_uuid_it);
    PERFETTO_DCHECK(index < TrackEventData::kMaxNumExtraCounters);

    auto opt_resolved = track_event_tracker_->InternDescriptorTrackCounter(
        *track_uuid_it, kNullStringId, packet_sequence_id_);
    PERFETTO_CHECK(opt_resolved);
    TrackId track_id = *opt_resolved;

    double value = event_data_->extra_counter_values[index];
    context_->event_tracker->PushCounter(ts_, value, track_id);

    // Also import thread_time and thread_instruction_count counters into
    // slice columns to simplify JSON export.
    auto counter_track = storage_->track_table().FindById(track_id);
    StringId counter_name = counter_track->name();
    if (counter_name == parser_->counter_name_thread_time_id_) {
      thread_timestamp_ = static_cast<int64_t>(value);
    } else if (counter_name ==
               parser_->counter_name_thread_instruction_count_id_) {
      thread_instruction_count_ = static_cast<int64_t>(value);
    }
  }

  base::Status ParseThreadBeginEvent() {
    if (!utid_) {
      return base::ErrStatus(
          "TrackEvent with phase B without thread association");
    }

    ASSIGN_OR_RETURN(auto track_id, ParseTrackAssociationBegin());
    auto opt_slice_id = context_->slice_tracker->Begin(
        ts_, track_id, category_id_, name_id_,
        [this](BoundInserter* inserter) { ParseTrackEventArgs(inserter); });
    if (opt_slice_id.has_value()) {
      auto rr =
          context_->storage->mutable_slice_table()->FindById(*opt_slice_id);
      if (thread_timestamp_) {
        rr->set_thread_ts(*thread_timestamp_);
      }
      if (thread_instruction_count_) {
        rr->set_thread_instruction_count(*thread_instruction_count_);
      }
      MaybeParseFlowEvents(opt_slice_id.value());
    }
    return base::OkStatus();
  }

  base::Status ParseThreadEndEvent() {
    if (!utid_) {
      return base::ErrStatus(
          "TrackEvent with phase E without thread association");
    }
    ASSIGN_OR_RETURN(auto track_id, ParseTrackAssociationEnd());
    auto opt_slice_id = context_->slice_tracker->End(
        ts_, track_id, category_id_, name_id_,
        [this](BoundInserter* inserter) { ParseTrackEventArgs(inserter); });
    if (!opt_slice_id)
      return base::OkStatus();

    MaybeParseFlowEvents(*opt_slice_id);
    auto* thread_slices = storage_->mutable_slice_table();
    auto opt_thread_slice_ref = thread_slices->FindById(*opt_slice_id);
    if (!opt_thread_slice_ref) {
      // This means that the end event did not match a corresponding track event
      // begin packet so we likely closed the wrong slice. There's not much we
      // can do about this beyond flag it as a stat.
      context_->storage->IncrementStats(stats::track_event_thread_invalid_end);
      return base::OkStatus();
    }

    tables::SliceTable::RowReference slice_ref = *opt_thread_slice_ref;
    std::optional<int64_t> tts = slice_ref.thread_ts();
    if (tts && thread_timestamp_) {
      int64_t delta = *thread_timestamp_ - *tts;
      if (delta != 0) {
        slice_ref.set_thread_dur(delta);
      }
    }
    std::optional<int64_t> tic = slice_ref.thread_instruction_count();
    if (tic && thread_instruction_count_) {
      slice_ref.set_thread_instruction_delta(
          *event_data_->thread_instruction_count - *tic);
    }
    return base::OkStatus();
  }

  base::Status ParseThreadCompleteEvent() {
    if (!utid_) {
      return base::ErrStatus(
          "TrackEvent with phase X without thread association");
    }

    auto duration_ns = legacy_event_.duration_us() * 1000;
    if (duration_ns < 0)
      return base::ErrStatus("TrackEvent with phase X with negative duration");

    ASSIGN_OR_RETURN(auto track_id, ParseTrackAssociationForLegacy());
    auto opt_slice_id = context_->slice_tracker->Scoped(
        ts_, track_id, category_id_, name_id_, duration_ns,
        [this](BoundInserter* inserter) { ParseTrackEventArgs(inserter); });
    if (opt_slice_id.has_value()) {
      auto rr =
          context_->storage->mutable_slice_table()->FindById(*opt_slice_id);
      PERFETTO_CHECK(rr);
      if (thread_timestamp_) {
        rr->set_thread_ts(*thread_timestamp_);
        rr->set_thread_dur(legacy_event_.thread_duration_us() * 1000);
      }
      if (thread_instruction_count_) {
        rr->set_thread_instruction_count(*thread_instruction_count_);
        rr->set_thread_instruction_delta(
            legacy_event_.thread_instruction_delta());
      }
      MaybeParseFlowEvents(opt_slice_id.value());
    }
    return base::OkStatus();
  }

  std::optional<uint64_t> GetLegacyEventId() {
    if (legacy_event_.has_unscoped_id())
      return legacy_event_.unscoped_id();
    // TODO(andrewbb): Catapult doesn't support global_id and local_id on flow
    // events. We could add support in trace processor (e.g. because there seem
    // to be some callsites supplying local_id in chromium), but we would have
    // to consider the process ID for local IDs and use a separate ID scope for
    // global_id and unscoped_id.
    return std::nullopt;
  }

  base::Status ParseFlowEventV1(char phase) {
    auto opt_source_id = GetLegacyEventId();
    if (!opt_source_id) {
      storage_->IncrementStats(stats::flow_invalid_id);
      return base::ErrStatus("Invalid id for flow event v1");
    }
    FlowId flow_id = context_->flow_tracker->GetFlowIdForV1Event(
        opt_source_id.value(), category_id_, name_id_);
    ASSIGN_OR_RETURN(auto track_id, ParseTrackAssociationForLegacy());
    switch (phase) {
      case 's':
        context_->flow_tracker->Begin(track_id, flow_id);
        break;
      case 't':
        context_->flow_tracker->Step(track_id, flow_id);
        break;
      case 'f':
        context_->flow_tracker->End(track_id, flow_id,
                                    legacy_event_.bind_to_enclosing(),
                                    /* close_flow = */ false);
        break;
    }
    return base::OkStatus();
  }

  void MaybeParseTrackEventFlows(SliceId slice_id) {
    if (event_.has_flow_ids_old() || event_.has_flow_ids()) {
      auto it =
          event_.has_flow_ids() ? event_.flow_ids() : event_.flow_ids_old();
      for (; it; ++it) {
        FlowId flow_id = *it;
        if (!context_->flow_tracker->IsActive(flow_id)) {
          context_->flow_tracker->Begin(slice_id, flow_id);
          continue;
        }
        context_->flow_tracker->Step(slice_id, flow_id);
      }
    }
    if (event_.has_terminating_flow_ids_old() ||
        event_.has_terminating_flow_ids()) {
      auto it = event_.has_terminating_flow_ids()
                    ? event_.terminating_flow_ids()
                    : event_.terminating_flow_ids_old();
      for (; it; ++it) {
        FlowId flow_id = *it;
        if (!context_->flow_tracker->IsActive(flow_id)) {
          // If we should terminate a flow, do not begin a new one if it's not
          // active already.
          continue;
        }
        context_->flow_tracker->End(slice_id, flow_id,
                                    /* close_flow = */ true);
      }
    }
  }

  void MaybeParseFlowEventV2(SliceId slice_id) {
    if (!legacy_event_.has_bind_id()) {
      return;
    }
    if (!legacy_event_.has_flow_direction()) {
      storage_->IncrementStats(stats::flow_without_direction);
      return;
    }

    auto bind_id = legacy_event_.bind_id();
    switch (legacy_event_.flow_direction()) {
      case LegacyEvent::FLOW_OUT:
        context_->flow_tracker->Begin(slice_id, bind_id);
        break;
      case LegacyEvent::FLOW_INOUT:
        context_->flow_tracker->Step(slice_id, bind_id);
        break;
      case LegacyEvent::FLOW_IN:
        context_->flow_tracker->End(slice_id, bind_id,
                                    /* close_flow = */ false);
        break;
      default:
        storage_->IncrementStats(stats::flow_without_direction);
    }
  }

  void MaybeParseFlowEvents(SliceId slice_id) {
    MaybeParseFlowEventV2(slice_id);
    MaybeParseTrackEventFlows(slice_id);
  }

  base::Status ParseThreadInstantEvent(char phase) {
    // Handle instant events as slices with zero duration, so that they end
    // up nested underneath their parent slices.
    int64_t duration_ns = 0;
    int64_t tidelta = 0;
    std::optional<tables::SliceTable::Id> opt_slice_id;
    auto args_inserter = [this, phase](BoundInserter* inserter) {
      ParseTrackEventArgs(inserter);
      // For legacy MARK event, add phase for JSON exporter.
      if (phase == 'R') {
        std::string phase_string(1, static_cast<char>(phase));
        StringId phase_id = storage_->InternString(phase_string.c_str());
        inserter->AddArg(parser_->legacy_event_phase_key_id_,
                         Variadic::String(phase_id));
      }
    };
    ASSIGN_OR_RETURN(auto track_id, ParseTrackAssociationInstant());
    opt_slice_id =
        context_->slice_tracker->Scoped(ts_, track_id, category_id_, name_id_,
                                        duration_ns, std::move(args_inserter));
    if (!opt_slice_id) {
      return base::OkStatus();
    }
    if (utid_) {
      auto rr =
          context_->storage->mutable_slice_table()->FindById(*opt_slice_id);
      if (thread_timestamp_) {
        rr->set_thread_ts(*thread_timestamp_);
        rr->set_thread_dur(duration_ns);
      }
      if (thread_instruction_count_) {
        rr->set_thread_instruction_count(*thread_instruction_count_);
        rr->set_thread_instruction_delta(tidelta);
      }
    }
    MaybeParseFlowEvents(opt_slice_id.value());
    return base::OkStatus();
  }

  base::Status ParseAsyncBeginEvent(char phase) {
    auto args_inserter = [this, phase](BoundInserter* inserter) {
      ParseTrackEventArgs(inserter);

      if (phase == 'b')
        return;
      PERFETTO_DCHECK(phase == 'S');
      // For legacy ASYNC_BEGIN, add phase for JSON exporter.
      std::string phase_string(1, static_cast<char>(phase));
      StringId phase_id = storage_->InternString(phase_string.c_str());
      inserter->AddArg(parser_->legacy_event_phase_key_id_,
                       Variadic::String(phase_id));
    };
    ASSIGN_OR_RETURN(auto track_id, ParseTrackAssociationBegin());
    auto opt_slice_id = context_->slice_tracker->Begin(
        ts_, track_id, category_id_, name_id_, args_inserter);
    if (!opt_slice_id.has_value()) {
      return base::OkStatus();
    }
    MaybeParseFlowEvents(opt_slice_id.value());
    // For the time being, we only create vtrack slice rows if we need to
    // store thread timestamps/counters.
    if (legacy_event_.use_async_tts()) {
      auto* vtrack_slices = storage_->mutable_virtual_track_slices();
      PERFETTO_DCHECK(!vtrack_slices->slice_count() ||
                      vtrack_slices->slice_ids().back() < opt_slice_id.value());
      int64_t tts = thread_timestamp_.value_or(0);
      int64_t tic = thread_instruction_count_.value_or(0);
      vtrack_slices->AddVirtualTrackSlice(opt_slice_id.value(), tts,
                                          kPendingThreadDuration, tic,
                                          kPendingThreadInstructionDelta);
    }
    return base::OkStatus();
  }

  base::Status ParseAsyncEndEvent() {
    ASSIGN_OR_RETURN(auto track_id, ParseTrackAssociationEnd());
    auto opt_slice_id = context_->slice_tracker->End(
        ts_, track_id, category_id_, name_id_,
        [this](BoundInserter* inserter) { ParseTrackEventArgs(inserter); });
    if (!opt_slice_id)
      return base::OkStatus();

    MaybeParseFlowEvents(*opt_slice_id);
    if (legacy_event_.use_async_tts()) {
      auto* vtrack_slices = storage_->mutable_virtual_track_slices();
      int64_t tts = event_data_->thread_timestamp.value_or(0);
      int64_t tic = event_data_->thread_instruction_count.value_or(0);
      vtrack_slices->UpdateThreadDeltasForSliceId(*opt_slice_id, tts, tic);
    }
    return base::OkStatus();
  }

  base::Status ParseAsyncStepEvent(char phase) {
    // Parse step events as instant events. Reconstructing the begin/end times
    // of the child slice would be too complicated, see b/178540838. For JSON
    // export, we still record the original step's phase in an arg.
    ASSIGN_OR_RETURN(auto track_id, ParseTrackAssociationInstant());
    int64_t duration_ns = 0;
    context_->slice_tracker->Scoped(
        ts_, track_id, category_id_, name_id_, duration_ns,
        [this, phase](BoundInserter* inserter) {
          ParseTrackEventArgs(inserter);

          PERFETTO_DCHECK(phase == 'T' || phase == 'p');
          std::string phase_string(1, static_cast<char>(phase));
          StringId phase_id = storage_->InternString(phase_string.c_str());
          inserter->AddArg(parser_->legacy_event_phase_key_id_,
                           Variadic::String(phase_id));
        });
    // Step events don't support thread timestamps, so no need to add a row to
    // virtual_track_slices.
    return base::OkStatus();
  }

  base::Status ParseAsyncInstantEvent() {
    // Handle instant events as slices with zero duration, so that they end
    // up nested underneath their parent slices.
    ASSIGN_OR_RETURN(auto track_id, ParseTrackAssociationInstant());
    int64_t duration_ns = 0;
    int64_t tidelta = 0;
    auto opt_slice_id = context_->slice_tracker->Scoped(
        ts_, track_id, category_id_, name_id_, duration_ns,
        [this](BoundInserter* inserter) { ParseTrackEventArgs(inserter); });
    if (!opt_slice_id.has_value()) {
      return base::OkStatus();
    }
    MaybeParseFlowEvents(opt_slice_id.value());
    if (legacy_event_.use_async_tts()) {
      auto* vtrack_slices = storage_->mutable_virtual_track_slices();
      PERFETTO_DCHECK(!vtrack_slices->slice_count() ||
                      vtrack_slices->slice_ids().back() < opt_slice_id.value());
      int64_t tts = thread_timestamp_.value_or(0);
      int64_t tic = thread_instruction_count_.value_or(0);
      vtrack_slices->AddVirtualTrackSlice(opt_slice_id.value(), tts,
                                          duration_ns, tic, tidelta);
    }
    return base::OkStatus();
  }

  base::Status ParseMetadataEvent() {
    ProcessTracker* procs = context_->process_tracker.get();

    if (name_id_ == kNullStringId)
      return base::ErrStatus("Metadata event without name");

    // Parse process and thread names from correspondingly named events.
    NullTermStringView event_name = storage_->GetString(name_id_);
    PERFETTO_DCHECK(event_name.data());
    if (event_name == "thread_name") {
      if (!utid_) {
        return base::ErrStatus(
            "thread_name metadata event without thread association");
      }

      auto it = event_.debug_annotations();
      if (!it) {
        return base::ErrStatus(
            "thread_name metadata event without debug annotations");
      }
      protos::pbzero::DebugAnnotation::Decoder annotation(*it);
      auto thread_name = annotation.string_value();
      if (!thread_name.size)
        return base::OkStatus();
      auto thread_name_id = storage_->InternString(thread_name);
      procs->UpdateThreadName(*utid_, thread_name_id,
                              ThreadNamePriority::kTrackDescriptorThreadType);
      return base::OkStatus();
    }
    if (event_name == "process_name") {
      if (!upid_) {
        return base::ErrStatus(
            "process_name metadata event without process association");
      }

      auto it = event_.debug_annotations();
      if (!it) {
        return base::ErrStatus(
            "process_name metadata event without debug annotations");
      }
      protos::pbzero::DebugAnnotation::Decoder annotation(*it);
      auto process_name = annotation.string_value();
      if (!process_name.size)
        return base::OkStatus();
      auto process_name_id =
          storage_->InternString(base::StringView(process_name));
      // Don't override system-provided names.
      procs->SetProcessNameIfUnset(*upid_, process_name_id);
      return base::OkStatus();
    }
    // Other metadata events are proxied via the raw table for JSON export.
    ParseLegacyEventAsRawEvent();
    return base::OkStatus();
  }

  base::Status ParseLegacyEventAsRawEvent() {
    if (!utid_)
      return base::ErrStatus("raw legacy event without thread association");

    tables::ChromeRawTable::Id id =
        storage_->mutable_chrome_raw_table()
            ->Insert({ts_, parser_->raw_legacy_event_id_, *utid_, 0})
            .id;

    ArgsTracker args_tracker(context_);
    auto inserter = args_tracker.AddArgsTo(id);
    inserter
        .AddArg(parser_->legacy_event_category_key_id_,
                Variadic::String(category_id_))
        .AddArg(parser_->legacy_event_name_key_id_, Variadic::String(name_id_));

    std::string phase_string(1, static_cast<char>(legacy_event_.phase()));
    StringId phase_id = storage_->InternString(phase_string.c_str());
    inserter.AddArg(parser_->legacy_event_phase_key_id_,
                    Variadic::String(phase_id));

    if (legacy_event_.has_duration_us()) {
      inserter.AddArg(parser_->legacy_event_duration_ns_key_id_,
                      Variadic::Integer(legacy_event_.duration_us() * 1000));
    }

    if (thread_timestamp_) {
      inserter.AddArg(parser_->legacy_event_thread_timestamp_ns_key_id_,
                      Variadic::Integer(*thread_timestamp_));
      if (legacy_event_.has_thread_duration_us()) {
        inserter.AddArg(
            parser_->legacy_event_thread_duration_ns_key_id_,
            Variadic::Integer(legacy_event_.thread_duration_us() * 1000));
      }
    }

    if (thread_instruction_count_) {
      inserter.AddArg(parser_->legacy_event_thread_instruction_count_key_id_,
                      Variadic::Integer(*thread_instruction_count_));
      if (legacy_event_.has_thread_instruction_delta()) {
        inserter.AddArg(
            parser_->legacy_event_thread_instruction_delta_key_id_,
            Variadic::Integer(legacy_event_.thread_instruction_delta()));
      }
    }

    if (legacy_event_.use_async_tts()) {
      inserter.AddArg(parser_->legacy_event_use_async_tts_key_id_,
                      Variadic::Boolean(true));
    }

    bool has_id = false;
    if (legacy_event_.has_unscoped_id()) {
      // Unscoped ids are either global or local depending on the phase. Pass
      // them through as unscoped IDs to JSON export to preserve this behavior.
      inserter.AddArg(parser_->legacy_event_unscoped_id_key_id_,
                      Variadic::UnsignedInteger(legacy_event_.unscoped_id()));
      has_id = true;
    } else if (legacy_event_.has_global_id()) {
      inserter.AddArg(parser_->legacy_event_global_id_key_id_,
                      Variadic::UnsignedInteger(legacy_event_.global_id()));
      has_id = true;
    } else if (legacy_event_.has_local_id()) {
      inserter.AddArg(parser_->legacy_event_local_id_key_id_,
                      Variadic::UnsignedInteger(legacy_event_.local_id()));
      has_id = true;
    }

    if (has_id && legacy_event_.has_id_scope() &&
        legacy_event_.id_scope().size) {
      inserter.AddArg(
          parser_->legacy_event_id_scope_key_id_,
          Variadic::String(storage_->InternString(legacy_event_.id_scope())));
    }

    // No need to parse legacy_event.instant_event_scope() because we import
    // instant events into the slice table.

    ParseTrackEventArgs(&inserter);
    return base::OkStatus();
  }

  void ParseTrackEventArgs(BoundInserter* inserter) {
    auto log_errors = [this](const base::Status& status) {
      if (status.ok())
        return;
      // Log error but continue parsing the other args.
      storage_->IncrementStats(stats::track_event_parser_errors);
      PERFETTO_DLOG("ParseTrackEventArgs error: %s", status.c_message());
    };

    if (event_.has_source_location_iid()) {
      log_errors(AddSourceLocationArgs(event_.source_location_iid(), inserter));
    }

    if (event_.has_task_execution()) {
      log_errors(ParseTaskExecutionArgs(event_.task_execution(), inserter));
    }
    if (event_.has_log_message()) {
      log_errors(ParseLogMessage(event_.log_message(), inserter));
    }
    if (event_.has_chrome_histogram_sample()) {
      log_errors(
          ParseHistogramName(event_.chrome_histogram_sample(), inserter));
    }
    if (event_.has_chrome_active_processes()) {
      protos::pbzero::ChromeActiveProcesses::Decoder message(
          event_.chrome_active_processes());
      for (auto it = message.pid(); it; ++it) {
        parser_->AddActiveProcess(ts_, *it);
      }
    }
    if (event_.has_correlation_id()) {
      base::StackString<512> id_str("tp:#%" PRIu64, event_.correlation_id());
      inserter->AddArg(parser_->correlation_id_key_id_,
                       Variadic::String(context_->storage->InternString(
                           id_str.string_view())));
    }
    if (event_.has_correlation_id_str()) {
      inserter->AddArg(parser_->correlation_id_key_id_,
                       Variadic::String(storage_->InternString(
                           base::StringView(event_.correlation_id_str()))));
    }
    if (event_.has_correlation_id_str_iid()) {
      auto* decoder = sequence_state_->LookupInternedMessage<
          protos::pbzero::InternedData::kCorrelationIdStrFieldNumber,
          protos::pbzero::InternedString>(event_.correlation_id_str_iid());
      inserter->AddArg(parser_->correlation_id_key_id_,
                       Variadic::String(storage_->InternString(base::StringView(
                           reinterpret_cast<const char*>(decoder->str().data),
                           decoder->str().size))));
    }
    if (legacy_trace_source_id_) {
      inserter->AddArg(parser_->legacy_trace_source_id_key_id_,
                       Variadic::Integer(*legacy_trace_source_id_));
    }

    // Parse callstack if present
    // For end events, use end_callsite_id key; otherwise use callsite_id key
    StringId callstack_key = event_.type() == TrackEvent::TYPE_SLICE_END
                                 ? parser_->end_callsite_id_key_id_
                                 : parser_->callsite_id_key_id_;
    log_errors(ParseCallstack(inserter, callstack_key));

    ArgsParser args_writer(ts_, *inserter, *storage_, sequence_state_,
                           /*support_json=*/true);
    int unknown_extensions = 0;
    log_errors(parser_->args_parser_.ParseMessage(
        blob_, ".perfetto.protos.TrackEvent", &parser_->reflect_fields_,
        args_writer, &unknown_extensions));
    if (unknown_extensions > 0) {
      context_->storage->IncrementStats(stats::unknown_extension_fields,
                                        unknown_extensions);
    }

    {
      auto key = parser_->args_parser_.EnterDictionary("debug");
      util::DebugAnnotationParser parser(parser_->args_parser_);
      for (auto it = event_.debug_annotations(); it; ++it) {
        log_errors(parser.Parse(*it, args_writer));
      }
    }

    if (legacy_passthrough_utid_) {
      inserter->AddArg(parser_->legacy_event_passthrough_utid_id_,
                       Variadic::UnsignedInteger(*legacy_passthrough_utid_),
                       ArgsTracker::UpdatePolicy::kSkipIfExists);
    }
  }

  base::Status ParseTaskExecutionArgs(ConstBytes task_execution,
                                      BoundInserter* inserter) {
    protos::pbzero::TaskExecution::Decoder task(task_execution);
    uint64_t iid = task.posted_from_iid();
    if (!iid)
      return base::ErrStatus("TaskExecution with invalid posted_from_iid");

    auto* decoder = sequence_state_->LookupInternedMessage<
        protos::pbzero::InternedData::kSourceLocationsFieldNumber,
        protos::pbzero::SourceLocation>(iid);
    if (!decoder)
      return base::ErrStatus("TaskExecution with invalid posted_from_iid");

    StringId file_name_id = kNullStringId;
    StringId function_name_id = kNullStringId;
    uint32_t line_number = 0;

    std::string file_name = NormalizePathSeparators(decoder->file_name());
    file_name_id = storage_->InternString(base::StringView(file_name));
    function_name_id = storage_->InternString(decoder->function_name());
    line_number = decoder->line_number();

    inserter->AddArg(parser_->task_file_name_args_key_id_,
                     Variadic::String(file_name_id));
    inserter->AddArg(parser_->task_function_name_args_key_id_,
                     Variadic::String(function_name_id));
    inserter->AddArg(parser_->task_line_number_args_key_id_,
                     Variadic::UnsignedInteger(line_number));
    return base::OkStatus();
  }

  base::Status AddSourceLocationArgs(uint64_t iid, BoundInserter* inserter) {
    if (!iid)
      return base::ErrStatus("SourceLocation with invalid iid");

    auto* decoder = sequence_state_->LookupInternedMessage<
        protos::pbzero::InternedData::kSourceLocationsFieldNumber,
        protos::pbzero::SourceLocation>(iid);
    if (!decoder)
      return base::ErrStatus("SourceLocation with invalid iid");

    StringId file_name_id = kNullStringId;
    StringId function_name_id = kNullStringId;
    uint32_t line_number = 0;

    std::string file_name = NormalizePathSeparators(decoder->file_name());
    file_name_id = storage_->InternString(base::StringView(file_name));
    function_name_id = storage_->InternString(decoder->function_name());
    line_number = decoder->line_number();

    inserter->AddArg(parser_->source_location_file_name_key_id_,
                     Variadic::String(file_name_id));
    inserter->AddArg(parser_->source_location_function_name_key_id_,
                     Variadic::String(function_name_id));
    inserter->AddArg(parser_->source_location_line_number_key_id_,
                     Variadic::UnsignedInteger(line_number));
    return base::OkStatus();
  }

  base::Status ParseLogMessage(ConstBytes blob, BoundInserter* inserter) {
    if (!utid_)
      return base::ErrStatus("LogMessage without thread association");

    protos::pbzero::LogMessage::Decoder message(blob);

    auto* body_decoder = sequence_state_->LookupInternedMessage<
        protos::pbzero::InternedData::kLogMessageBodyFieldNumber,
        protos::pbzero::LogMessageBody>(message.body_iid());
    if (!body_decoder)
      return base::ErrStatus("LogMessage with invalid body_iid");

    const StringId log_message_id =
        storage_->InternString(body_decoder->body());
    inserter->AddArg(parser_->log_message_body_key_id_,
                     Variadic::String(log_message_id));

    StringId source_location_id = kNullStringId;
    if (message.has_source_location_iid()) {
      auto* source_location_decoder = sequence_state_->LookupInternedMessage<
          protos::pbzero::InternedData::kSourceLocationsFieldNumber,
          protos::pbzero::SourceLocation>(message.source_location_iid());
      if (!source_location_decoder)
        return base::ErrStatus("LogMessage with invalid source_location_iid");
      const std::string source_location =
          source_location_decoder->file_name().ToStdString() + ":" +
          std::to_string(source_location_decoder->line_number());
      source_location_id =
          storage_->InternString(base::StringView(source_location));

      inserter->AddArg(parser_->log_message_source_location_file_name_key_id_,
                       Variadic::String(storage_->InternString(
                           source_location_decoder->file_name())));
      inserter->AddArg(
          parser_->log_message_source_location_function_name_key_id_,
          Variadic::String(storage_->InternString(
              source_location_decoder->function_name())));
      inserter->AddArg(
          parser_->log_message_source_location_line_number_key_id_,
          Variadic::Integer(source_location_decoder->line_number()));
    }

    // The track event log message doesn't specify any priority. UI never
    // displays priorities < 2 (VERBOSE in android). Let's make all the track
    // event logs show up as INFO.
    int32_t priority = protos::pbzero::AndroidLogPriority::PRIO_INFO;
    if (message.has_prio()) {
      priority = ToAndroidLogPriority(
          static_cast<protos::pbzero::LogMessage::Priority>(message.prio()));
      inserter->AddArg(parser_->log_message_priority_id_,
                       Variadic::Integer(priority));
    }

    storage_->mutable_android_log_table()->Insert(
        {ts_, *utid_,
         /*priority*/ static_cast<uint32_t>(priority),
         /*tag_id*/ source_location_id, log_message_id});

    return base::OkStatus();
  }

  base::Status ParseHistogramName(ConstBytes blob, BoundInserter* inserter) {
    protos::pbzero::ChromeHistogramSample::Decoder sample(blob);
    if (!sample.has_name_iid())
      return base::OkStatus();

    if (sample.has_name()) {
      return base::ErrStatus(
          "name is already set for ChromeHistogramSample: only one of name and "
          "name_iid can be set.");
    }

    auto* decoder = sequence_state_->LookupInternedMessage<
        protos::pbzero::InternedData::kHistogramNamesFieldNumber,
        protos::pbzero::HistogramName>(sample.name_iid());
    if (!decoder)
      return base::ErrStatus("HistogramName with invalid name_iid");

    inserter->AddArg(parser_->histogram_name_key_id_,
                     Variadic::String(storage_->InternString(decoder->name())));
    return base::OkStatus();
  }

  base::Status ParseCallstack(BoundInserter* inserter, StringId key_id) {
    // Handle interned callstack via callstack_iid
    if (event_.has_callstack_iid()) {
      auto* callstack_decoder = sequence_state_->LookupInternedMessage<
          protos::pbzero::InternedData::kCallstacksFieldNumber,
          protos::pbzero::Callstack>(event_.callstack_iid());
      if (!callstack_decoder) {
        return base::ErrStatus("TrackEvent with invalid callstack_iid");
      }
      // Get or create the callsite from the interned callstack
      auto* stack_profile_state =
          sequence_state_->GetCustomState<StackProfileSequenceState>();
      if (!stack_profile_state) {
        return base::ErrStatus(
            "TrackEvent with callstack but no StackProfileSequenceState");
      }
      // Pass upid as optional - will work with or without process association
      auto callsite_id = stack_profile_state->FindOrInsertCallstack(
          upid_, event_.callstack_iid());
      if (!callsite_id) {
        return base::ErrStatus("Failed to intern callstack");
      }
      inserter->AddArg(key_id, Variadic::UnsignedInteger(callsite_id->value));
      return base::OkStatus();
    }

    // Handle inline callstack
    // Inline callstacks are simple: just function names and source locations
    if (event_.has_callstack()) {
      protos::pbzero::TrackEvent::Callstack::Decoder callstack(
          event_.callstack());
      DummyMemoryMapping* dummy_mapping =
          parser_->GetOrCreateInlineCallstackDummyMapping();

      std::optional<CallsiteId> callsite_id;
      uint32_t depth = 0;
      for (auto frame_it = callstack.frames(); frame_it; ++frame_it, ++depth) {
        protos::pbzero::TrackEvent::Callstack::Frame::Decoder frame(*frame_it);
        std::optional<base::StringView> source_file;
        if (frame.has_source_file()) {
          source_file = frame.source_file();
        }
        std::optional<uint32_t> line_number;
        if (frame.has_line_number()) {
          line_number = frame.line_number();
        }
        FrameId frame_id = dummy_mapping->InternDummyFrame(
            frame.function_name(), source_file, line_number);
        callsite_id = context_->stack_profile_tracker->InternCallsite(
            callsite_id, frame_id, depth);
      }
      // Add the final callsite_id as an arg
      if (callsite_id) {
        inserter->AddArg(key_id, Variadic::UnsignedInteger(callsite_id->value));
      }
      return base::OkStatus();
    }
    return base::OkStatus();
  }

  TraceProcessorContext* context_;
  TrackEventTracker* track_event_tracker_;
  TraceStorage* storage_;
  TrackEventParser* parser_;
  ArgsTranslationTable* args_translation_table_;
  int64_t ts_;
  const TrackEventData* event_data_;
  PacketSequenceStateGeneration* sequence_state_;
  ConstBytes blob_;
  TrackEvent::Decoder event_;
  LegacyEvent::Decoder legacy_event_;
  protos::pbzero::TrackEventDefaults::Decoder* defaults_;

  // Importing state.
  StringId category_id_;
  StringId name_id_;
  uint64_t track_uuid_;
  std::optional<UniqueTid> utid_;
  std::optional<UniqueTid> upid_;
  std::optional<int64_t> thread_timestamp_;
  std::optional<int64_t> thread_instruction_count_;
  bool fallback_to_legacy_pid_tid_tracks_ = false;
  std::optional<int64_t> legacy_trace_source_id_;

  // All events in legacy JSON require a thread ID, but for some types of
  // events (e.g. async events or process/global-scoped instants), we don't
  // store it in the slice/track model. To pass the utid through to the json
  // export, we store it in an arg.
  std::optional<UniqueTid> legacy_passthrough_utid_;

  uint32_t packet_sequence_id_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_EVENT_IMPORTER_H_
