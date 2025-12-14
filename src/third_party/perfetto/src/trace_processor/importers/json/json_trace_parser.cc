/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/trace_processor/importers/json/json_trace_parser.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/variant.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/flow_tracker.h"
#include "src/trace_processor/importers/common/legacy_v8_cpu_profile_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/systrace/systrace_line.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/json_parser.h"
#include "src/trace_processor/util/json_utils.h"

namespace perfetto::trace_processor {

namespace {

std::optional<uint64_t> MaybeExtractFlowIdentifier(StringPool* pool,
                                                   const JsonEvent& value,
                                                   bool version2) {
  const auto& id_key = version2 ? value.bind_id : value.id;
  auto id_type = version2 ? value.bind_id_type : value.id_type;
  switch (id_type) {
    case JsonEvent::IdType::kNone:
      return std::nullopt;
    case JsonEvent::IdType::kString: {
      NullTermStringView str = pool->Get(id_key.id_str);
      return base::CStringToUInt64(str.c_str(), 16);
    }
    case JsonEvent::IdType::kUint64:
      return id_key.id_uint64;
  }
  PERFETTO_FATAL("For GCC");
}

inline std::string_view GetStringValue(const json::JsonValue& value) {
  if (const auto* str = std::get_if<std::string_view>(&value)) {
    return *str;
  }
  return {};
}

TrackCompressor::AsyncSliceType AsyncSliceTypeForPhase(char phase) {
  switch (phase) {
    case 'b':
      return TrackCompressor::AsyncSliceType::kBegin;
    case 'e':
      return TrackCompressor::AsyncSliceType::kEnd;
    case 'n':
      return TrackCompressor::AsyncSliceType::kInstant;
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace

JsonTraceParser::JsonTraceParser(TraceProcessorContext* context)
    : context_(context),
      systrace_line_parser_(context),
      process_sort_index_hint_id_(
          context->storage->InternString("process_sort_index_hint")),
      thread_sort_index_hint_id_(
          context->storage->InternString("thread_sort_index_hint")) {}

JsonTraceParser::~JsonTraceParser() = default;

void JsonTraceParser::ParseSystraceLine(int64_t, SystraceLine line) {
  systrace_line_parser_.ParseLine(line);
}

void JsonTraceParser::ParseJsonPacket(int64_t timestamp, JsonEvent event) {
  ProcessTracker* procs = context_->process_tracker.get();
  TraceStorage* storage = context_->storage.get();
  SliceTracker* slice_tracker = context_->slice_tracker.get();
  FlowTracker* flow_tracker = context_->flow_tracker.get();

  if (event.pid_is_string_id) {
    UniquePid upid = procs->GetOrCreateProcess(event.pid);
    procs->SetProcessMetadata(
        upid, storage->GetString(StringPool::Id::Raw(event.pid)),
        base::StringView());
  }
  if (event.tid_is_string_id) {
    UniqueTid event_utid = procs->GetOrCreateThread(event.tid);
    procs->UpdateThreadName(event_utid, StringPool::Id::Raw(event.tid),
                            ThreadNamePriority::kOther);
  }
  UniqueTid utid = procs->UpdateThread(event.tid, event.pid);

  // Only used for 'B', 'E', and 'X' events so wrap in lambda so it gets
  // ignored in other cases. This lambda is only safe to call within the
  // scope of this function due to the capture by reference.
  auto args_inserter = [&](ArgsTracker::BoundInserter* inserter) {
    if (event.args_size > 0) {
      json::AddJsonValueToArgs(
          it_, event.args.get(), event.args.get() + event.args_size,
          /* flat_key = */ "args",
          /* key = */ "args", context_->storage.get(), inserter);
    }
  };

  base::StringView id;
  if (event.id_type == JsonEvent::IdType::kString) {
    id = context_->storage->GetString(event.id.id_str);
  }
  StringId slice_name_id = event.name == kNullStringId
                               ? storage->InternString("[No name]")
                               : event.name;
  switch (event.phase) {
    case 'B': {  // TRACE_EVENT_BEGIN.
      TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
      auto slice_id = slice_tracker->Begin(timestamp, track_id, event.cat,
                                           slice_name_id, args_inserter);
      if (slice_id && event.tts != std::numeric_limits<int64_t>::max()) {
        auto rr = context_->storage->mutable_slice_table()->FindById(*slice_id);
        rr->set_thread_ts(event.tts);
      }
      MaybeAddFlow(storage->mutable_string_pool(), track_id, event);
      break;
    }
    case 'E': {  // TRACE_EVENT_END.
      TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
      auto slice_id = slice_tracker->End(timestamp, track_id, event.cat,
                                         event.name, args_inserter);
      // Now try to update thread_dur if we have a tts field.
      if (slice_id && event.tts != std::numeric_limits<int64_t>::max()) {
        auto rr = *storage->mutable_slice_table()->FindById(*slice_id);
        if (auto start_tts = rr.thread_ts(); start_tts) {
          rr.set_thread_dur(event.tts - *start_tts);
        }
      }
      break;
    }
    case 'b':
    case 'e':
    case 'n': {
      if (!event.pid_exists ||
          event.async_cookie_type == JsonEvent::AsyncCookieType::kNone) {
        context_->storage->IncrementStats(stats::json_parser_failure);
        return;
      }
      UniquePid upid = context_->process_tracker->GetOrCreateProcess(event.pid);
      TrackId track_id;
      if (event.async_cookie_type == JsonEvent::AsyncCookieType::kId ||
          event.async_cookie_type == JsonEvent::AsyncCookieType::kId2Global) {
        track_id = context_->track_compressor->InternLegacyAsyncTrack(
            event.name, upid, event.async_cookie,
            false /* source_id_is_process_scoped */,
            kNullStringId /* source_scope */,
            AsyncSliceTypeForPhase(event.phase));
      } else {
        PERFETTO_DCHECK(event.async_cookie_type ==
                        JsonEvent::AsyncCookieType::kId2Local);
        track_id = context_->track_compressor->InternLegacyAsyncTrack(
            event.name, upid, event.async_cookie,
            true /* source_id_is_process_scoped */,
            kNullStringId /* source_scope */,
            AsyncSliceTypeForPhase(event.phase));
      }
      if (event.phase == 'b') {
        slice_tracker->Begin(timestamp, track_id, event.cat, slice_name_id,
                             args_inserter);
        MaybeAddFlow(storage->mutable_string_pool(), track_id, event);
      } else if (event.phase == 'e') {
        slice_tracker->End(timestamp, track_id, event.cat, event.name,
                           args_inserter);
        // We don't handle tts here as we do in the 'E'
        // case above as it's not well defined for async slices.
      } else {
        context_->slice_tracker->Scoped(timestamp, track_id, event.cat,
                                        event.name, 0, args_inserter);
        MaybeAddFlow(storage->mutable_string_pool(), track_id, event);
      }
      break;
    }
    case 'X': {  // TRACE_EVENT (scoped event).
      if (event.dur == std::numeric_limits<int64_t>::max()) {
        context_->storage->IncrementStats(stats::json_parser_failure);
        return;
      }
      TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
      auto slice_id =
          slice_tracker->Scoped(timestamp, track_id, event.cat, slice_name_id,
                                event.dur, args_inserter);
      if (slice_id) {
        auto rr = context_->storage->mutable_slice_table()->FindById(*slice_id);
        if (event.tts != std::numeric_limits<int64_t>::max()) {
          rr->set_thread_ts(event.tts);
        }
        if (event.tdur != std::numeric_limits<int64_t>::max()) {
          rr->set_thread_dur(event.tdur);
        }
      }
      MaybeAddFlow(storage->mutable_string_pool(), track_id, event);
      break;
    }
    case 'C': {  // TRACE_EVENT_COUNTER
      if (event.args_size == 0) {
        context_->storage->IncrementStats(stats::json_parser_failure);
        return;
      }
      it_.Reset(event.args.get(), event.args.get() + event.args_size);
      if (!it_.ParseStart()) {
        context_->storage->IncrementStats(stats::json_parser_failure);
        return;
      }
      std::string counter_name_prefix =
          context_->storage->GetString(event.name).ToStdString();
      if (!id.empty()) {
        counter_name_prefix += " id: " + id.ToStdString();
      }
      counter_name_prefix += " ";
      for (;;) {
        double counter;
        switch (it_.ParseObjectFieldWithoutRecursing()) {
          case json::ReturnCode::kOk:
          case json::ReturnCode::kEndOfScope:
            break;
          case json::ReturnCode::kError:
            context_->storage->IncrementStats(stats::json_parser_failure);
            continue;
          case json::ReturnCode::kIncompleteInput:
            PERFETTO_FATAL("Unexpected incomplete input in JSON object");
        }
        if (it_.eof()) {
          break;
        }
        switch (it_.value().index()) {
          case base::variant_index<json::JsonValue, std::string_view>(): {
            auto opt = base::StringToDouble(std::string(
                base::unchecked_get<std::string_view>(it_.value())));
            if (!opt.has_value()) {
              context_->storage->IncrementStats(stats::json_parser_failure);
              continue;
            }
            counter = opt.value();
            break;
          }
          case base::variant_index<json::JsonValue, double>():
            counter = base::unchecked_get<double>(it_.value());
            break;
          case base::variant_index<json::JsonValue, int64_t>():
            counter =
                static_cast<double>(base::unchecked_get<int64_t>(it_.value()));
            break;
          default:
            context_->storage->IncrementStats(stats::json_parser_failure);
            continue;
        }
        std::string counter_name = counter_name_prefix;
        counter_name += it_.key();
        StringId nid = context_->storage->InternString(counter_name);
        context_->event_tracker->PushProcessCounterForThread(
            EventTracker::JsonCounter{nid}, timestamp, counter, utid);
      }
      break;
    }
    case 'R':
    case 'I':
    case 'i': {  // TRACE_EVENT_INSTANT
      TrackId track_id;
      if (event.scope == JsonEvent::Scope::kGlobal) {
        track_id = context_->track_tracker->InternTrack(
            tracks::kLegacyGlobalInstantsBlueprint, tracks::Dimensions(),
            tracks::BlueprintName(),
            [this](ArgsTracker::BoundInserter& inserter) {
              inserter.AddArg(
                  context_->storage->InternString("source"),
                  Variadic::String(context_->storage->InternString("chrome")));
            });
      } else if (event.scope == JsonEvent::Scope::kProcess) {
        if (!event.pid_exists) {
          context_->storage->IncrementStats(stats::json_parser_failure);
          break;
        }
        UniquePid upid =
            context_->process_tracker->GetOrCreateProcess(event.pid);
        track_id = context_->track_tracker->InternTrack(
            tracks::kChromeProcessInstantBlueprint, tracks::Dimensions(upid),
            tracks::BlueprintName(),
            [this](ArgsTracker::BoundInserter& inserter) {
              inserter.AddArg(
                  context_->storage->InternString("source"),
                  Variadic::String(context_->storage->InternString("chrome")));
            });
      } else if (event.scope == JsonEvent::Scope::kThread ||
                 event.scope == JsonEvent::Scope::kNone) {
        if (!event.tid_exists) {
          context_->storage->IncrementStats(stats::json_parser_failure);
          return;
        }
        track_id = context_->track_tracker->InternThreadTrack(utid);
        auto slice_id = slice_tracker->Scoped(timestamp, track_id, event.cat,
                                              slice_name_id, 0, args_inserter);
        if (slice_id) {
          if (event.tts != std::numeric_limits<int64_t>::max()) {
            auto rr =
                context_->storage->mutable_slice_table()->FindById(*slice_id);
            rr->set_thread_ts(event.tts);
          }
        }
        break;
      } else {
        context_->storage->IncrementStats(stats::json_parser_failure);
        return;
      }
      context_->slice_tracker->Scoped(timestamp, track_id, event.cat,
                                      event.name, 0, args_inserter);
      break;
    }
    case 's': {  // TRACE_EVENT_FLOW_START
      TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
      auto opt_source_id =
          MaybeExtractFlowIdentifier(context_->storage->mutable_string_pool(),
                                     event, /* version2 = */ false);
      if (opt_source_id) {
        FlowId flow_id = flow_tracker->GetFlowIdForV1Event(
            opt_source_id.value(), event.cat, event.name);
        flow_tracker->Begin(track_id, flow_id);
      } else {
        context_->storage->IncrementStats(stats::flow_invalid_id);
      }
      break;
    }
    case 't': {  // TRACE_EVENT_FLOW_STEP
      TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
      auto opt_source_id =
          MaybeExtractFlowIdentifier(context_->storage->mutable_string_pool(),
                                     event, /* version2 = */ false);
      if (opt_source_id) {
        FlowId flow_id = flow_tracker->GetFlowIdForV1Event(
            opt_source_id.value(), event.cat, event.name);
        flow_tracker->Step(track_id, flow_id);
      } else {
        context_->storage->IncrementStats(stats::flow_invalid_id);
      }
      break;
    }
    case 'f': {  // TRACE_EVENT_FLOW_END
      TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
      auto opt_source_id =
          MaybeExtractFlowIdentifier(context_->storage->mutable_string_pool(),
                                     event, /* version2 = */ false);
      if (opt_source_id) {
        FlowId flow_id = flow_tracker->GetFlowIdForV1Event(
            opt_source_id.value(), event.cat, event.name);
        flow_tracker->End(track_id, flow_id, event.bind_enclosing_slice,
                          /* close_flow = */ false);
      } else {
        context_->storage->IncrementStats(stats::flow_invalid_id);
      }
      break;
    }
    case 'M': {  // Metadata events (process and thread names).
      if (event.args_size == 0) {
        break;
      }
      base::StringView name = storage->GetString(event.name);
      if (name != "thread_name" && name != "process_name" &&
          name != "process_sort_index" && name != "thread_sort_index") {
        break;
      }
      it_.Reset(event.args.get(), event.args.get() + event.args_size);
      if (!it_.ParseStart()) {
        context_->storage->IncrementStats(stats::json_parser_failure);
        break;
      }
      for (;;) {
        switch (it_.ParseObjectFieldWithoutRecursing()) {
          case json::ReturnCode::kEndOfScope:
          case json::ReturnCode::kOk:
            break;
          case json::ReturnCode::kError:
            context_->storage->IncrementStats(stats::json_parser_failure);
            continue;
          case json::ReturnCode::kIncompleteInput:
            PERFETTO_FATAL("Unexpected incomplete input in JSON object");
        }
        if (it_.eof()) {
          break;
        }
        if (name == "process_sort_index" || name == "thread_sort_index") {
          if (it_.key() != "sort_index") {
            continue;
          }
          int64_t sort_index;
          switch (it_.value().index()) {
            case base::variant_index<json::JsonValue, int64_t>():
              sort_index = base::unchecked_get<int64_t>(it_.value());
              break;
            case base::variant_index<json::JsonValue, double>():
              sort_index = static_cast<int64_t>(
                  base::unchecked_get<double>(it_.value()));
              break;
            default:
              context_->storage->IncrementStats(stats::json_parser_failure);
              continue;
          }
          if (name == "process_sort_index") {
            UniquePid upid = procs->GetOrCreateProcess(event.pid);
            auto inserter = procs->AddArgsToProcess(upid);
            inserter.AddArg(process_sort_index_hint_id_,
                            Variadic::Integer(sort_index));
          } else {
            auto inserter = procs->AddArgsToThread(utid);
            inserter.AddArg(thread_sort_index_hint_id_,
                            Variadic::Integer(sort_index));
          }
        } else {
          if (it_.key() != "name") {
            continue;
          }
          std::string_view args_name = GetStringValue(it_.value());
          if (args_name.empty()) {
            context_->storage->IncrementStats(stats::json_parser_failure);
            continue;
          }
          if (name == "thread_name") {
            auto thread_name_id = context_->storage->InternString(args_name);
            procs->UpdateThreadName(utid, thread_name_id,
                                    ThreadNamePriority::kOther);
          } else if (name == "process_name") {
            UniquePid upid = procs->GetOrCreateProcess(event.pid);
            procs->SetProcessMetadata(
                upid, base::StringView(args_name.data(), args_name.size()),
                base::StringView());
          }
        }
      }
    }
  }
}

void JsonTraceParser::MaybeAddFlow(StringPool* pool,
                                   TrackId track_id,
                                   const JsonEvent& event) {
  auto opt_bind_id =
      MaybeExtractFlowIdentifier(pool, event, /* version2 = */ true);
  if (opt_bind_id) {
    FlowTracker* flow_tracker = context_->flow_tracker.get();
    if (event.flow_in && event.flow_out) {
      flow_tracker->Step(track_id, opt_bind_id.value());
    } else if (event.flow_out) {
      flow_tracker->Begin(track_id, opt_bind_id.value());
    } else if (event.flow_in) {
      // bind_enclosing_slice is always true for v2 flow events
      flow_tracker->End(track_id, opt_bind_id.value(), true,
                        /* close_flow = */ false);
    } else {
      context_->storage->IncrementStats(stats::flow_without_direction);
    }
  }
}

}  // namespace perfetto::trace_processor
