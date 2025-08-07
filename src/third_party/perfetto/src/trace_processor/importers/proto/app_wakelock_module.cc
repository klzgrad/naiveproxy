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

#include "src/trace_processor/importers/proto/app_wakelock_module.h"

#include "perfetto/ext/base/hash.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "protos/perfetto/trace/android/app_wakelock_data.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

using ::perfetto::protos::pbzero::AppWakelockBundle;
using ::perfetto::protos::pbzero::AppWakelockInfo;
using ::perfetto::protos::pbzero::TracePacket;
using ::protozero::ConstBytes;

AppWakelockModule::AppWakelockModule(TraceProcessorContext* context)
    : context_(context),
      arg_flags_(context->storage->InternString("flags")),
      arg_owner_pid_(context->storage->InternString("owner_pid")),
      arg_owner_uid_(context->storage->InternString("owner_uid")),
      arg_work_uid_(context->storage->InternString("work_uid")) {
  RegisterForField(TracePacket::kAppWakelockBundleFieldNumber, context);
}

ModuleResult AppWakelockModule::TokenizePacket(
    const protos::pbzero::TracePacket::Decoder& decoder,
    TraceBlobView*,
    int64_t ts,
    RefPtr<PacketSequenceStateGeneration> state,
    uint32_t field_id) {
  if (field_id != TracePacket::kAppWakelockBundleFieldNumber) {
    return ModuleResult::Ignored();
  }

  AppWakelockBundle::Decoder evt(decoder.app_wakelock_bundle());

  bool parse_error = false;
  auto iid_iter = evt.intern_id(&parse_error);
  auto timestamp_iter = evt.encoded_ts(&parse_error);
  if (parse_error) {
    context_->storage->IncrementStats(stats::app_wakelock_parse_error);
    return ModuleResult::Handled();
  }

  for (; timestamp_iter && iid_iter; ++timestamp_iter, ++iid_iter) {
    uint64_t encoded_ts = *timestamp_iter;
    uint32_t intern_id = *iid_iter;

    int64_t real_ts = ts + static_cast<int64_t>(encoded_ts >> 1);
    bool acquired = encoded_ts & 0x1;

    auto* interned = state->LookupInternedMessage<
        protos::pbzero::InternedData::kAppWakelockInfoFieldNumber,
        protos::pbzero::AppWakelockInfo>(intern_id);
    if (interned == nullptr) {
      context_->storage->IncrementStats(stats::app_wakelock_unknown_id);
      continue;
    }

    packet_buffer_->set_timestamp(static_cast<uint64_t>(real_ts));
    auto* event = packet_buffer_->set_app_wakelock_bundle();
    size_t length = static_cast<size_t>(interned->end() - interned->begin());
    event->set_info()->AppendRawProtoBytes(interned->begin(), length);
    event->set_acquired(acquired);
    PushPacketBufferForSort(real_ts, state);
  }

  return ModuleResult::Handled();
}

void AppWakelockModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData&,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kAppWakelockBundleFieldNumber:
      ParseWakelockBundle(ts, decoder.app_wakelock_bundle());
      return;
  }
}

void AppWakelockModule::ParseWakelockBundle(int64_t ts, ConstBytes blob) {
  AppWakelockBundle::Decoder event(blob);
  AppWakelockInfo::Decoder info(event.info());

  // TODO(rzuklie): Create tracks per uid or per pid.
  static constexpr auto kBlueprint = TrackCompressor::SliceBlueprint(
      "app_wakelock_events", tracks::DimensionBlueprints(),
      tracks::StaticNameBlueprint("app_wakelock_events"));

  // The data source doesn't specify a cookie, packets should instead be matched
  // by their corresponding attributes. Use these to form a cookie for pairing.
  std::size_t cookie = base::Hasher::Combine(info.tag().ToStdStringView(),
                                             info.flags(), info.owner_pid(),
                                             info.owner_uid(), info.work_uid());

  if (!event.acquired()) {
    TrackId track_id = context_->track_compressor->InternEnd(
        kBlueprint, tracks::Dimensions(), static_cast<int64_t>(cookie));
    context_->slice_tracker->End(ts, track_id);
    return;
  }

  TrackId track_id = context_->track_compressor->InternBegin(
      kBlueprint, tracks::Dimensions(), static_cast<int64_t>(cookie));
  StringId name_id = context_->storage->InternString(info.tag());
  context_->slice_tracker->Begin(
      ts, track_id, kNullStringId, name_id,
      [this, &info](ArgsTracker::BoundInserter* args) {
        args->AddArg(arg_flags_, Variadic::Integer(info.flags()));
        if (info.has_owner_pid()) {
          args->AddArg(arg_owner_pid_, Variadic::Integer(info.owner_pid()));
        }
        if (info.has_owner_uid()) {
          args->AddArg(arg_owner_uid_, Variadic::Integer(info.owner_uid()));
        }
        if (info.has_work_uid()) {
          args->AddArg(arg_work_uid_, Variadic::Integer(info.work_uid()));
        }
      });
}

void AppWakelockModule::PushPacketBufferForSort(
    int64_t timestamp,
    RefPtr<PacketSequenceStateGeneration> state) {
  std::pair<std::unique_ptr<uint8_t[]>, size_t> v =
      packet_buffer_.SerializeAsUniquePtr();
  context_->sorter->PushTracePacket(
      timestamp, std::move(state),
      TraceBlobView(TraceBlob::TakeOwnership(std::move(v.first), v.second)));
  packet_buffer_.Reset();
}

}  // namespace perfetto::trace_processor
