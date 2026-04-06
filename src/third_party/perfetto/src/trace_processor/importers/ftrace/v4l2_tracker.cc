/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <cinttypes>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/flow_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/ftrace/v4l2_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/v4l2.pbzero.h"

namespace perfetto::trace_processor {

namespace {
using protos::pbzero::FtraceEvent;
using protos::pbzero::V4l2DqbufFtraceEvent;
using protos::pbzero::V4l2QbufFtraceEvent;
using protos::pbzero::Vb2V4l2BufDoneFtraceEvent;
using protos::pbzero::Vb2V4l2BufQueueFtraceEvent;
using protos::pbzero::Vb2V4l2DqbufFtraceEvent;
using protos::pbzero::Vb2V4l2QbufFtraceEvent;
using protozero::ConstBytes;
}  // namespace

V4l2Tracker::V4l2Tracker(TraceProcessorContext* context)
    : context_(context),
      buf_event_ids_(*context->storage),
      buf_type_ids_(*context_->storage),
      buf_field_ids_(*context->storage),
      tc_type_ids_(*context->storage) {}

V4l2Tracker::~V4l2Tracker() = default;

void V4l2Tracker::ParseV4l2Event(uint64_t fld_id,
                                 int64_t timestamp,
                                 uint32_t pid,
                                 const ConstBytes& bytes) {
  switch (fld_id) {
    case FtraceEvent::kV4l2QbufFieldNumber: {
      V4l2QbufFtraceEvent::Decoder pb_evt(bytes);
      BufferEvent evt;
      evt.device_minor = pb_evt.minor();
      evt.index = pb_evt.index();
      evt.type = pb_evt.type();
      evt.bytesused = pb_evt.bytesused();
      evt.flags = pb_evt.flags();
      evt.field = pb_evt.field();
      evt.timestamp = pb_evt.timestamp();
      evt.sequence = pb_evt.sequence();
      evt.timecode_flags = pb_evt.timecode_flags();
      evt.timecode_frames = pb_evt.timecode_frames();
      evt.timecode_hours = pb_evt.timecode_hours();
      evt.timecode_minutes = pb_evt.timecode_minutes();
      evt.timecode_seconds = pb_evt.timecode_seconds();
      evt.timecode_type = pb_evt.timecode_type();
      evt.timecode_userbits0 = pb_evt.timecode_userbits0();
      evt.timecode_userbits1 = pb_evt.timecode_userbits1();
      evt.timecode_userbits2 = pb_evt.timecode_userbits2();
      evt.timecode_userbits3 = pb_evt.timecode_userbits3();

      base::StackString<64> buf_name(
          "VIDIOC_QBUF minor=%" PRId32 " seq=%" PRIu32 " type=%" PRIu32
          " index=%" PRIu32,
          evt.device_minor, evt.sequence, *evt.type, *evt.index);

      StringId buf_name_id =
          context_->storage->InternString(buf_name.string_view());
      std::optional<SliceId> slice_id =
          AddSlice(buf_name_id, timestamp, pid, evt);

      uint64_t hash = base::MurmurHashCombine(evt.device_minor, evt.sequence,
                                              *evt.type, *evt.index);

      QueuedBuffer queued_buffer;
      queued_buffer.queue_slice_id = slice_id;

      queued_buffers_.Insert(hash, std::move(queued_buffer));
      break;
    }
    case FtraceEvent::kV4l2DqbufFieldNumber: {
      V4l2DqbufFtraceEvent::Decoder pb_evt(bytes);
      BufferEvent evt;
      evt.device_minor = pb_evt.minor();
      evt.index = pb_evt.index();
      evt.type = pb_evt.type();
      evt.bytesused = pb_evt.bytesused();
      evt.flags = pb_evt.flags();
      evt.field = pb_evt.field();
      evt.timestamp = pb_evt.timestamp();
      evt.sequence = pb_evt.sequence();
      evt.timecode_flags = pb_evt.timecode_flags();
      evt.timecode_frames = pb_evt.timecode_frames();
      evt.timecode_hours = pb_evt.timecode_hours();
      evt.timecode_minutes = pb_evt.timecode_minutes();
      evt.timecode_seconds = pb_evt.timecode_seconds();
      evt.timecode_type = pb_evt.timecode_type();
      evt.timecode_userbits0 = pb_evt.timecode_userbits0();
      evt.timecode_userbits1 = pb_evt.timecode_userbits1();
      evt.timecode_userbits2 = pb_evt.timecode_userbits2();
      evt.timecode_userbits3 = pb_evt.timecode_userbits3();

      base::StackString<64> buf_name(
          "VIDIOC_DQBUF minor=%" PRId32 " seq=%" PRIu32 " type=%" PRIu32
          " index=%" PRIu32,
          evt.device_minor, evt.sequence, *evt.type, *evt.index);

      StringId buf_name_id =
          context_->storage->InternString(buf_name.string_view());
      std::optional<SliceId> slice_id =
          AddSlice(buf_name_id, timestamp, pid, evt);

      uint64_t hash = base::MurmurHashCombine(evt.device_minor, evt.sequence,
                                              *evt.type, *evt.index);

      const QueuedBuffer* queued_buffer = queued_buffers_.Find(hash);
      if (queued_buffer) {
        if (queued_buffer->queue_slice_id && slice_id) {
          context_->flow_tracker->InsertFlow(*queued_buffer->queue_slice_id,
                                             *slice_id);
        }

        queued_buffers_.Erase(hash);
      }
      break;
    }
    case FtraceEvent::kVb2V4l2BufQueueFieldNumber: {
      Vb2V4l2BufQueueFtraceEvent::Decoder pb_evt(bytes);
      BufferEvent evt;
      evt.device_minor = pb_evt.minor();
      evt.index = std::nullopt;
      evt.type = std::nullopt;
      evt.bytesused = std::nullopt;
      evt.flags = pb_evt.flags();
      evt.field = pb_evt.field();
      evt.timestamp = pb_evt.timestamp();
      evt.sequence = pb_evt.sequence();
      evt.timecode_flags = pb_evt.timecode_flags();
      evt.timecode_frames = pb_evt.timecode_frames();
      evt.timecode_hours = pb_evt.timecode_hours();
      evt.timecode_minutes = pb_evt.timecode_minutes();
      evt.timecode_seconds = pb_evt.timecode_seconds();
      evt.timecode_type = pb_evt.timecode_type();
      evt.timecode_userbits0 = pb_evt.timecode_userbits0();
      evt.timecode_userbits1 = pb_evt.timecode_userbits1();
      evt.timecode_userbits2 = pb_evt.timecode_userbits2();
      evt.timecode_userbits3 = pb_evt.timecode_userbits3();

      base::StackString<64> buf_name("vb2_v4l2_buf_queue minor=%" PRId32
                                     " seq=%" PRIu32 " type=0 index=0",
                                     evt.device_minor, evt.sequence);

      StringId buf_name_id =
          context_->storage->InternString(buf_name.string_view());
      AddSlice(buf_name_id, timestamp, pid, evt);
      break;
    }
    case FtraceEvent::kVb2V4l2BufDoneFieldNumber: {
      Vb2V4l2BufDoneFtraceEvent::Decoder pb_evt(bytes);
      BufferEvent evt;
      evt.device_minor = pb_evt.minor();
      evt.index = std::nullopt;
      evt.type = std::nullopt;
      evt.bytesused = std::nullopt;
      evt.flags = pb_evt.flags();
      evt.field = pb_evt.field();
      evt.timestamp = pb_evt.timestamp();
      evt.sequence = pb_evt.sequence();
      evt.timecode_flags = pb_evt.timecode_flags();
      evt.timecode_frames = pb_evt.timecode_frames();
      evt.timecode_hours = pb_evt.timecode_hours();
      evt.timecode_minutes = pb_evt.timecode_minutes();
      evt.timecode_seconds = pb_evt.timecode_seconds();
      evt.timecode_type = pb_evt.timecode_type();
      evt.timecode_userbits0 = pb_evt.timecode_userbits0();
      evt.timecode_userbits1 = pb_evt.timecode_userbits1();
      evt.timecode_userbits2 = pb_evt.timecode_userbits2();
      evt.timecode_userbits3 = pb_evt.timecode_userbits3();

      base::StackString<64> buf_name("vb2_v4l2_buf_done minor=%" PRId32
                                     " seq=%" PRIu32 " type=0 index=0",
                                     evt.device_minor, evt.sequence);

      StringId buf_name_id =
          context_->storage->InternString(buf_name.string_view());
      AddSlice(buf_name_id, timestamp, pid, evt);
      break;
    }
    case FtraceEvent::kVb2V4l2QbufFieldNumber: {
      Vb2V4l2QbufFtraceEvent::Decoder pb_evt(bytes);
      BufferEvent evt;
      evt.device_minor = pb_evt.minor();
      evt.index = std::nullopt;
      evt.type = std::nullopt;
      evt.bytesused = std::nullopt;
      evt.flags = pb_evt.flags();
      evt.field = pb_evt.field();
      evt.timestamp = pb_evt.timestamp();
      evt.sequence = pb_evt.sequence();
      evt.timecode_flags = pb_evt.timecode_flags();
      evt.timecode_frames = pb_evt.timecode_frames();
      evt.timecode_hours = pb_evt.timecode_hours();
      evt.timecode_minutes = pb_evt.timecode_minutes();
      evt.timecode_seconds = pb_evt.timecode_seconds();
      evt.timecode_type = pb_evt.timecode_type();
      evt.timecode_userbits0 = pb_evt.timecode_userbits0();
      evt.timecode_userbits1 = pb_evt.timecode_userbits1();
      evt.timecode_userbits2 = pb_evt.timecode_userbits2();
      evt.timecode_userbits3 = pb_evt.timecode_userbits3();

      base::StackString<64> buf_name("vb2_v4l2_qbuf minor=%" PRId32
                                     " seq=%" PRIu32 " type=0 index=0",
                                     evt.device_minor, evt.sequence);

      StringId buf_name_id =
          context_->storage->InternString(buf_name.string_view());
      AddSlice(buf_name_id, timestamp, pid, evt);
      break;
    }
    case FtraceEvent::kVb2V4l2DqbufFieldNumber: {
      Vb2V4l2DqbufFtraceEvent::Decoder pb_evt(bytes);
      BufferEvent evt;
      evt.device_minor = pb_evt.minor();
      evt.index = std::nullopt;
      evt.type = std::nullopt;
      evt.bytesused = std::nullopt;
      evt.flags = pb_evt.flags();
      evt.field = pb_evt.field();
      evt.timestamp = pb_evt.timestamp();
      evt.sequence = pb_evt.sequence();
      evt.timecode_flags = pb_evt.timecode_flags();
      evt.timecode_frames = pb_evt.timecode_frames();
      evt.timecode_hours = pb_evt.timecode_hours();
      evt.timecode_minutes = pb_evt.timecode_minutes();
      evt.timecode_seconds = pb_evt.timecode_seconds();
      evt.timecode_type = pb_evt.timecode_type();
      evt.timecode_userbits0 = pb_evt.timecode_userbits0();
      evt.timecode_userbits1 = pb_evt.timecode_userbits1();
      evt.timecode_userbits2 = pb_evt.timecode_userbits2();
      evt.timecode_userbits3 = pb_evt.timecode_userbits3();

      base::StackString<64> buf_name("vb2_v4l2_qbuf minor=%" PRId32
                                     " seq=%" PRIu32 " type=0 index=0",
                                     evt.device_minor, evt.sequence);

      StringId buf_name_id =
          context_->storage->InternString(buf_name.string_view());
      AddSlice(buf_name_id, timestamp, pid, evt);
      break;
    }
    default:
      break;
  }
}

std::optional<SliceId> V4l2Tracker::AddSlice(StringId buf_name_id,
                                             int64_t timestamp,
                                             uint32_t pid,
                                             const BufferEvent& evt) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);

  std::optional<SliceId> slice_id = context_->slice_tracker->Scoped(
      timestamp, track_id, buf_event_ids_.v4l2, buf_name_id, 0,
      [this, &evt](ArgsTracker::BoundInserter* inserter) {
        this->AddArgs(evt, inserter);
      });

  return slice_id;
}

void V4l2Tracker::AddArgs(const BufferEvent& evt,
                          ArgsTracker::BoundInserter* inserter) {
  inserter->AddArg(buf_event_ids_.device_minor,
                   Variadic::Integer(evt.device_minor));

  if (evt.index)
    inserter->AddArg(buf_event_ids_.index, Variadic::Integer(*evt.index));
  if (evt.type)
    inserter->AddArg(buf_event_ids_.type,
                     Variadic::String(buf_type_ids_.Map(*evt.type)));
  if (evt.bytesused)
    inserter->AddArg(buf_event_ids_.bytesused,
                     Variadic::Integer(*evt.bytesused));

  inserter->AddArg(buf_event_ids_.flags,
                   Variadic::String(InternBufFlags(evt.flags)));
  inserter->AddArg(buf_event_ids_.field,
                   Variadic::String(buf_field_ids_.Map(evt.field)));
  inserter->AddArg(buf_event_ids_.timestamp, Variadic::Integer(evt.timestamp));
  inserter->AddArg(buf_event_ids_.timecode_type,
                   Variadic::String(tc_type_ids_.Map(evt.timecode_type)));
  inserter->AddArg(buf_event_ids_.timecode_flags,
                   Variadic::String(InternTcFlags(evt.timecode_flags)));
  inserter->AddArg(buf_event_ids_.timecode_frames,
                   Variadic::Integer(evt.timecode_frames));
  inserter->AddArg(buf_event_ids_.timecode_seconds,
                   Variadic::Integer(evt.timecode_seconds));
  inserter->AddArg(buf_event_ids_.timecode_minutes,
                   Variadic::Integer(evt.timecode_minutes));
  inserter->AddArg(buf_event_ids_.timecode_hours,
                   Variadic::Integer(evt.timecode_hours));
  inserter->AddArg(buf_event_ids_.timecode_userbits0,
                   Variadic::Integer(evt.timecode_userbits0));
  inserter->AddArg(buf_event_ids_.timecode_userbits1,
                   Variadic::Integer(evt.timecode_userbits1));
  inserter->AddArg(buf_event_ids_.timecode_userbits2,
                   Variadic::Integer(evt.timecode_userbits2));
  inserter->AddArg(buf_event_ids_.timecode_userbits3,
                   Variadic::Integer(evt.timecode_userbits3));
  inserter->AddArg(buf_event_ids_.sequence, Variadic::Integer(evt.sequence));
}

V4l2Tracker::BufferEventStringIds::BufferEventStringIds(TraceStorage& storage)
    : v4l2(storage.InternString("Video 4 Linux 2")),
      v4l2_qbuf(storage.InternString("v4l2_qbuf")),
      v4l2_dqbuf(storage.InternString("v4l2_dqbuf")),
      device_minor(storage.InternString("minor")),
      index(storage.InternString("index")),
      type(storage.InternString("type")),
      bytesused(storage.InternString("bytesused")),
      flags(storage.InternString("flags")),
      field(storage.InternString("field")),
      timestamp(storage.InternString("timestamp")),
      timecode_type(storage.InternString("timecode_type")),
      timecode_flags(storage.InternString("timecode_flags")),
      timecode_frames(storage.InternString("timecode_frames")),
      timecode_seconds(storage.InternString("timecode_seconds")),
      timecode_minutes(storage.InternString("timecode_minutes")),
      timecode_hours(storage.InternString("timecode_hours")),
      timecode_userbits0(storage.InternString("timecode_userbits0")),
      timecode_userbits1(storage.InternString("timecode_userbits1")),
      timecode_userbits2(storage.InternString("timecode_userbits2")),
      timecode_userbits3(storage.InternString("timecode_userbits3")),
      sequence(storage.InternString("sequence")) {}

V4l2Tracker::BufferTypeStringIds::BufferTypeStringIds(TraceStorage& storage)
    : video_capture(storage.InternString("VIDEO_CAPTURE")),
      video_output(storage.InternString("VIDEO_OUTPUT")),
      video_overlay(storage.InternString("VIDEO_OVERLAY")),
      vbi_capture(storage.InternString("VBI_CAPTURE")),
      vbi_output(storage.InternString("VBI_OUTPUT")),
      sliced_vbi_capture(storage.InternString("SLICED_VBI_CAPTURE")),
      sliced_vbi_output(storage.InternString("SLICED_VBI_OUTPUT")),
      video_output_overlay(storage.InternString("VIDEO_OUTPUT_OVERLAY")),
      video_capture_mplane(storage.InternString("VIDEO_CAPTURE_MPLANE")),
      video_output_mplane(storage.InternString("VIDEO_OUTPUT_MPLANE")),
      sdr_capture(storage.InternString("SDR_CAPTURE")),
      sdr_output(storage.InternString("SDR_OUTPUT")),
      meta_capture(storage.InternString("META_CAPTURE")),
      meta_output(storage.InternString("META_OUTPUT")),
      priv(storage.InternString("PRIVATE")) {}

StringId V4l2Tracker::BufferTypeStringIds::Map(uint32_t buf_type) {
  // Values taken from linux/videodev2.h
  switch (buf_type) {
    case 1:
      return video_capture;
    case 2:
      return video_output;
    case 3:
      return video_overlay;
    case 4:
      return vbi_capture;
    case 5:
      return vbi_output;
    case 6:
      return sliced_vbi_capture;
    case 7:
      return sliced_vbi_output;
    case 8:
      return video_output_overlay;
    case 9:
      return video_capture_mplane;
    case 10:
      return video_output_mplane;
    case 11:
      return sdr_capture;
    case 12:
      return sdr_output;
    case 13:
      return meta_capture;
    case 14:
      return meta_output;
    case 0x80:
      return priv;
    default:
      return kNullStringId;
  }
}

V4l2Tracker::BufferFieldStringIds::BufferFieldStringIds(TraceStorage& storage)
    : any(storage.InternString("ANY")),
      none(storage.InternString("NONE")),
      top(storage.InternString("TOP")),
      bottom(storage.InternString("BOTTOM")),
      interlaced(storage.InternString("INTERLACED")),
      seq_tb(storage.InternString("SEQ_TB")),
      seq_bt(storage.InternString("SEQ_BT")),
      alternate(storage.InternString("ALTERNATE")),
      interlaced_tb(storage.InternString("INTERLACED_TB")),
      interlaced_bt(storage.InternString("INTERLACED_BT")) {}

StringId V4l2Tracker::BufferFieldStringIds::Map(uint32_t field) {
  // Values taken from linux/videodev2.h
  switch (field) {
    case 0:
      return any;
    case 1:
      return none;
    case 2:
      return top;
    case 3:
      return bottom;
    case 4:
      return interlaced;
    case 5:
      return seq_tb;
    case 6:
      return seq_bt;
    case 7:
      return alternate;
    case 8:
      return interlaced_tb;
    case 9:
      return interlaced_bt;
    default:
      return kNullStringId;
  }
}

V4l2Tracker::TimecodeTypeStringIds::TimecodeTypeStringIds(TraceStorage& storage)
    : type_24fps(storage.InternString("24FPS")),
      type_25fps(storage.InternString("25FPS")),
      type_30fps(storage.InternString("30FPS")),
      type_50fps(storage.InternString("50FPS")),
      type_60fps(storage.InternString("60FPS")) {}

StringId V4l2Tracker::TimecodeTypeStringIds::Map(uint32_t type) {
  switch (type) {
    case 1:
      return type_24fps;
    case 2:
      return type_25fps;
    case 3:
      return type_30fps;
    case 4:
      return type_50fps;
    case 5:
      return type_60fps;
    default:
      return kNullStringId;
  }
}

StringId V4l2Tracker::InternBufFlags(uint32_t flags) {
  std::vector<std::string> present_flags;

  if (flags & 0x00000001)
    present_flags.push_back("MAPPED");
  if (flags & 0x00000002)
    present_flags.push_back("QUEUED");
  if (flags & 0x00000004)
    present_flags.push_back("DONE");
  if (flags & 0x00000008)
    present_flags.push_back("KEYFRAME");
  if (flags & 0x00000010)
    present_flags.push_back("PFRAME");
  if (flags & 0x00000020)
    present_flags.push_back("BFRAME");
  if (flags & 0x00000040)
    present_flags.push_back("ERROR");
  if (flags & 0x00000080)
    present_flags.push_back("IN_REQUEST");
  if (flags & 0x00000100)
    present_flags.push_back("TIMECODE");
  if (flags & 0x00000200)
    present_flags.push_back("M2M_HOLD_CAPTURE_BUF");
  if (flags & 0x00000400)
    present_flags.push_back("PREPARED");
  if (flags & 0x00000800)
    present_flags.push_back("NO_CACHE_INVALIDATE");
  if (flags & 0x00001000)
    present_flags.push_back("NO_CACHE_CLEAN");
  if (flags & 0x0000e000)
    present_flags.push_back("TIMESTAMP_MASK");
  if (flags == 0x00000000)
    present_flags.push_back("TIMESTAMP_UNKNOWN");
  if (flags & 0x00002000)
    present_flags.push_back("TIMESTAMP_MONOTONIC");
  if (flags & 0x00004000)
    present_flags.push_back("TIMESTAMP_COPY");
  if (flags & 0x00070000)
    present_flags.push_back("TSTAMP_SRC_MASK");
  if (flags == 0x00000000)
    present_flags.push_back("TSTAMP_SRC_EOF");
  if (flags & 0x00010000)
    present_flags.push_back("TSTAMP_SRC_SOE");
  if (flags & 0x00100000)
    present_flags.push_back("LAST");
  if (flags & 0x00800000)
    present_flags.push_back("REQUEST_FD");

  return context_->storage->InternString(
      base::Join(present_flags, "|").c_str());
}

StringId V4l2Tracker::InternTcFlags(uint32_t flags) {
  std::vector<std::string> present_flags;

  if (flags == 0x0000)
    present_flags.push_back("USERBITS_USERDEFINED");
  if (flags & 0x0001)
    present_flags.push_back("FLAG_DROPFRAME");
  if (flags & 0x0002)
    present_flags.push_back("FLAG_COLORFRAME");
  if ((flags & 0x000C) == 0x0004)
    present_flags.push_back("USERBITS_field(01)");
  if ((flags & 0x000C) == 0x0008)
    present_flags.push_back("USERBITS_field(10)");
  if ((flags & 0x000C) == 0x000C)
    present_flags.push_back("USERBITS_field(11)");
  if (flags & 0x0008)
    present_flags.push_back("USERBITS_8BITCHARS");

  return context_->storage->InternString(
      base::Join(present_flags, "|").c_str());
}

}  // namespace perfetto::trace_processor
