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

#include "src/trace_processor/plugins/video_frame_importer/video_frame_module.h"

#include <cstdint>
#include <optional>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob_view.h"

#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/plugins/video_frame_importer/tables_py.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/third_party/android/frameworks/base/proto/tracing/frameworks_base_trace_packet.pbzero.h"

namespace perfetto::trace_processor {

using ::com::android::internal::pbzero::FrameworksBaseTracePacket;
using ::com::android::internal::pbzero::VideoFrame;
using ::com::android::internal::pbzero::VideoFrameError;
using ::perfetto::protos::pbzero::TracePacket;

VideoFrameModule::VideoFrameModule(ProtoImporterModuleContext* mc,
                                   TraceProcessorContext* context,
                                   tables::AndroidVideoFramesTable* table,
                                   std::vector<TraceBlobView>* au_data)
    : ProtoImporterModule(mc),
      context_(context),
      table_(table),
      au_data_(au_data) {
  RegisterForField(FrameworksBaseTracePacket::kVideoFrameFieldNumber);
  RegisterForField(FrameworksBaseTracePacket::kVideoFrameErrorFieldNumber);
}

VideoFrameModule::~VideoFrameModule() = default;

ModuleResult VideoFrameModule::TokenizePacket(const TokenizePacketArgs& args) {
  // A video frame belongs on the timeline at its presentation time, not the
  // packet's encode-drain timestamp. pts_us is that presentation time, on
  // CLOCK_MONOTONIC for SurfaceFlinger surface input. Translate it and push the
  // packet through the sorter at that time here, during tokenization, so the
  // table timestamp is one the sorter ordered. Anything without a convertible
  // pts keeps the packet timestamp via the default path.
  if (args.field.id() != FrameworksBaseTracePacket::kVideoFrameFieldNumber) {
    return ModuleResult::Ignored();
  }
  VideoFrame::Decoder frame(
      args.field.Cast<FrameworksBaseTracePacket::kVideoFrame>());
  if (!frame.has_pts_us()) {
    return ModuleResult::Ignored();
  }
  std::optional<int64_t> present = context_->clock_tracker->ToTraceTime(
      ClockTracker::ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_MONOTONIC),
      static_cast<int64_t>(frame.pts_us()) * 1000);
  if (!present.has_value()) {
    return ModuleResult::Ignored();
  }
  module_context_->trace_packet_stream->Push(
      *present,
      TracePacketData{std::move(*args.packet), std::move(args.state)});
  return ModuleResult::Handled();
}

void VideoFrameModule::ParseField(const ParseFieldArgs& args) {
  switch (args.field.id()) {
    case FrameworksBaseTracePacket::kVideoFrameFieldNumber:
      ParseVideoFrame(args.field.Cast<FrameworksBaseTracePacket::kVideoFrame>(),
                      args.ts, args.data);
      break;
    case FrameworksBaseTracePacket::kVideoFrameErrorFieldNumber:
      ParseVideoFrameError(
          args.field.Cast<FrameworksBaseTracePacket::kVideoFrameError>(),
          args.ts);
      break;
    default:
      break;
  }
}

void VideoFrameModule::ParseVideoFrame(protozero::ConstBytes bytes,
                                       int64_t ts,
                                       const TracePacketData& data) {
  VideoFrame::Decoder frame(bytes);

  const uint32_t display_id = frame.has_display_id() ? frame.display_id() : 0u;

  // display_name + codec_string arrive on the codec_config packet; cache
  // them so later frames of the stream inherit them.
  StreamInfo& info = stream_info_by_id_[display_id];
  if (frame.has_display_name()) {
    info.display_name =
        context_->storage->InternString(frame.display_name().ToStdStringView());
  }
  if (frame.has_codec_string()) {
    info.codec_string =
        context_->storage->InternString(frame.codec_string().ToStdStringView());
  }

  tables::AndroidVideoFramesTable::Row row;
  row.ts = ts;
  row.display_id = static_cast<int32_t>(display_id);
  if (info.display_name != kNullStringId) {
    row.display_name = info.display_name;
  }
  if (info.codec_string != kNullStringId) {
    row.codec_string = info.codec_string;
  }
  row.frame_number =
      frame.has_frame_number() ? static_cast<int64_t>(frame.frame_number()) : 0;

  // A VideoFrame is either a codec_config (is_config = 1) or one access
  // unit. Skip a frame with no payload: every row must have bytes so the
  // au_data side-vector stays parallel to the table, indexed by row id.
  protozero::ConstBytes payload = {};
  if (frame.has_au_data()) {
    payload = frame.au_data();
    row.codec = frame.has_codec() ? static_cast<int32_t>(frame.codec()) : 0;
    row.is_key_frame = frame.is_key_frame() ? 1 : 0;
    if (frame.has_pts_us()) {
      row.pts_us = static_cast<int64_t>(frame.pts_us());
    }
  } else if (frame.has_codec_config()) {
    payload = frame.codec_config();
    row.codec = frame.has_codec() ? static_cast<int32_t>(frame.codec()) : 0;
    row.is_config = 1;
  }
  if (payload.size == 0) {
    return;
  }

  // Parse-time per-stream cap: drop frames once a stream exceeds it. Report
  // the first drop per stream to the import logs (with the affected display)
  // and let later drops fall through silently.
  if (info.emitted_bytes + static_cast<int64_t>(payload.size) >
      max_stream_size_bytes_) {
    if (!info.size_cap_hit) {
      info.size_cap_hit = true;
      context_->import_logs_tracker->RecordParserLog(
          stats::android_video_parse_size_cap_hit, ts,
          [this, display_id](ArgsTracker::BoundInserter& inserter) {
            inserter.AddArg(context_->storage->InternString("display_id"),
                            Variadic::UnsignedInteger(display_id));
          });
    }
    return;
  }
  info.emitted_bytes += static_cast<int64_t>(payload.size);

  auto id = table_->Insert(row).id.value;
  // Signal frame output to trace doctor (see TraceStorage).
  context_->storage->set_has_android_video_frames();
  // au_data is parallel to the table, indexed by row id.
  PERFETTO_DCHECK(id == au_data_->size());
  const TraceBlobView& packet = data.packet;
  const uint8_t* base = packet.blob()->data();
  PERFETTO_DCHECK(payload.data >= base &&
                  payload.data + payload.size <= base + packet.blob()->size());
  au_data_->emplace_back(packet.slice(payload.data, payload.size));
}

void VideoFrameModule::ParseVideoFrameError(protozero::ConstBytes bytes,
                                            int64_t ts) {
  VideoFrameError::Decoder err(bytes);
  if (!err.has_reason())
    return;
  const uint32_t display_id = err.has_display_id() ? err.display_id() : 0u;
  // Each reason maps to its own stat.
  size_t stat;
  switch (err.reason()) {
    case VideoFrameError::SIZE_CAP_HIT:
      stat = stats::android_video_size_cap_hit;
      break;
    case VideoFrameError::CODEC_ERROR:
      stat = stats::android_video_codec_error;
      break;
    case VideoFrameError::DISPLAY_GONE:
      stat = stats::android_video_display_gone;
      break;
    case VideoFrameError::NO_ENCODER:
      stat = stats::android_video_no_encoder;
      break;
    case VideoFrameError::DISPLAY_NOT_FOUND:
      stat = stats::android_video_display_not_found;
      break;
    case VideoFrameError::ENCODER_SETUP_FAILED:
      stat = stats::android_video_encoder_setup_failed;
      break;
    case VideoFrameError::VIRTUAL_DISPLAY_FAILED:
      stat = stats::android_video_virtual_display_failed;
      break;
    default:
      return;  // REASON_UNKNOWN or future enumerator
  }
  // Producer-reported failure: record to the import logs (which also bumps the
  // reason's stat), with the affected display as a queryable arg.
  context_->import_logs_tracker->RecordCollectionLog(
      stat, ts, [this, display_id](ArgsTracker::BoundInserter& inserter) {
        inserter.AddArg(context_->storage->InternString("display_id"),
                        Variadic::UnsignedInteger(display_id));
      });
}

}  // namespace perfetto::trace_processor
