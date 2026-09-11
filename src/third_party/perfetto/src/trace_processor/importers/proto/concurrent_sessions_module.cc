/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/concurrent_sessions_module.h"

#include <cstdint>
#include <string>

#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/perfetto/concurrent_session_event.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/state_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

using ::perfetto::protos::pbzero::ConcurrentSessionEvent;
using ::perfetto::protos::pbzero::TracePacket;

namespace {

// One state track per session. The UI groups tracks of this type under
// System > Concurrent tracing sessions (see state_tracks.ts).
constexpr auto kSessionTrackBlueprint = tracks::StateBlueprint(
    "concurrent_tracing_sessions",
    tracks::DimensionBlueprints(tracks::LongDimensionBlueprint("session_id")),
    tracks::DynamicNameBlueprint());

}  // namespace

ConcurrentSessionsModule::ConcurrentSessionsModule(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      context_(context),
      arg_consumer_uid_(context->storage->InternString("consumer_uid")),
      arg_num_data_sources_(context->storage->InternString("num_data_sources")),
      state_disabled_(context->storage->InternString("DISABLED")),
      state_configured_(context->storage->InternString("CONFIGURED")),
      state_started_(context->storage->InternString("STARTED")),
      state_disabling_waiting_stop_acks_(
          context->storage->InternString("DISABLING_WAITING_STOP_ACKS")),
      state_cloned_read_only_(
          context->storage->InternString("CLONED_READ_ONLY")) {
  RegisterForField(TracePacket::kConcurrentSessionEventFieldNumber);
}

void ConcurrentSessionsModule::ParseField(const ParseFieldArgs& args) {
  if (args.field.id() == TracePacket::kConcurrentSessionEventFieldNumber) {
    ParseConcurrentSessionEvent(
        args.ts, args.field.Cast<TracePacket::kConcurrentSessionEvent>());
  }
}

void ConcurrentSessionsModule::ParseConcurrentSessionEvent(
    int64_t ts,
    protozero::ConstBytes blob) {
  ConcurrentSessionEvent::Decoder event(blob);

  StringId state_name = kNullStringId;
  switch (event.state()) {
    case ConcurrentSessionEvent::STATE_DISABLED:
      state_name = state_disabled_;
      break;
    case ConcurrentSessionEvent::STATE_CONFIGURED:
      state_name = state_configured_;
      break;
    case ConcurrentSessionEvent::STATE_STARTED:
      state_name = state_started_;
      break;
    case ConcurrentSessionEvent::STATE_DISABLING_WAITING_STOP_ACKS:
      state_name = state_disabling_waiting_stop_acks_;
      break;
    case ConcurrentSessionEvent::STATE_CLONED_READ_ONLY:
      state_name = state_cloned_read_only_;
      break;
    default:
      // STATE_UNSPECIFIED or a state added by a newer version of the proto.
      context_->import_logs_tracker->RecordParserLog(
          stats::concurrent_session_event_unknown_state, ts,
          [&](ArgsTracker::BoundInserter& inserter) {
            inserter.AddArg(context_->storage->InternString("state"),
                            Variadic::Integer(event.state()));
          });
      return;
  }

  // Clones share their parent's name: suffix them to tell the tracks apart.
  // A clone's first event, which fixes the track name, is CLONED_READ_ONLY.
  std::string name = event.session_name().ToStdString();
  if (name.empty())
    name = "Session " + std::to_string(event.session_id());
  if (event.state() == ConcurrentSessionEvent::STATE_CLONED_READ_ONLY)
    name += " (clone)";
  StringId track_name = context_->storage->InternString(base::StringView(name));

  TrackId track_id = context_->track_tracker->InternTrack(
      kSessionTrackBlueprint,
      tracks::Dimensions(static_cast<int64_t>(event.session_id())),
      tracks::DynamicName(track_name));

  context_->state_tracker->UpdateState(
      ts, track_id, state_name, kNullStringId,
      [this, &event](ArgsTracker::BoundInserter* args) {
        args->AddArg(arg_consumer_uid_,
                     Variadic::Integer(event.consumer_uid()));
        args->AddArg(arg_num_data_sources_,
                     Variadic::Integer(event.num_data_sources()));
      });

  // DISABLED is terminal: close it at the same timestamp, so it renders as a
  // zero-width marker rather than an open-ended state.
  if (event.state() == ConcurrentSessionEvent::STATE_DISABLED)
    context_->state_tracker->UpdateState(ts, track_id, kNullStringId);
}

}  // namespace perfetto::trace_processor
