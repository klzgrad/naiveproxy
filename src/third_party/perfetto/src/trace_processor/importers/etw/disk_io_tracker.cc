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

#include "src/trace_processor/importers/etw/disk_io_tracker.h"

#include <optional>

#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/etw/etw.pbzero.h"
#include "protos/perfetto/trace/etw/etw_event.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"

namespace perfetto::trace_processor {

namespace {

// The value of the "Category" field for disk I/O events.
constexpr char kCategory[] = "ETW Disk I/O";

const auto kBlueprint = TrackCompressor::SliceBlueprint(
    "etw_diskio",
    tracks::DimensionBlueprints(tracks::kThreadDimensionBlueprint));

enum EventType {
  kRead = 10,
  kWrite = 11,
  kReadInit = 12,
  kWriteInit = 13,
  kFlush = 14,
  kFlushInit = 15,
};

// Returns a readable description for a disk I/O event type.
const char* GetEventTypeString(EventType event_type) {
  switch (event_type) {
    case kRead:
      return "DiskRead";
    case kWrite:
      return "DiskWrite";
    case kReadInit:
      return "DiskReadInit";
    case kWriteInit:
      return "DiskWriteInit";
    case kFlush:
      return "DiskFlush";
    case kFlushInit:
      return "DiskFlushInit";
  }
  return nullptr;
}

}  // namespace

DiskIoTracker::DiskIoTracker(TraceProcessorContext* context)
    : context_(context),
      disk_number_arg_(context_->storage->InternString("Disk Number")),
      irp_flags_arg_(context_->storage->InternString("Irp Flags")),
      transfer_size_arg_(context_->storage->InternString("Transfer Size")),
      byte_offset_arg_(context_->storage->InternString("Byte Offset")),
      file_object_arg_(context_->storage->InternString("File Object")),
      irp_ptr_arg_(context_->storage->InternString("Irp Ptr")),
      response_time_arg_(
          context_->storage->InternString("Response Time (microseconds)")),
      issuing_thread_id_arg_(
          context_->storage->InternString("Issuing Thread ID")) {}

void DiskIoTracker::ParseDiskIo(int64_t timestamp,
                                UniqueTid utid,
                                protozero::ConstBytes blob) {
  protos::pbzero::DiskIoEtwEvent::Decoder decoder(blob);
  if (!decoder.has_opcode()) {
    return;
  }
  const auto opcode = static_cast<EventType>(decoder.opcode());
  const auto irp =
      decoder.has_irp_ptr() ? std::optional(decoder.irp_ptr()) : std::nullopt;
  const auto disk_number = decoder.has_disk_number()
                               ? std::optional(decoder.disk_number())
                               : std::nullopt;
  const auto irp_flags = decoder.has_irp_flags()
                             ? std::optional(decoder.irp_flags())
                             : std::nullopt;
  const auto transfer_size = decoder.has_transfer_size()
                                 ? std::optional(decoder.transfer_size())
                                 : std::nullopt;
  const auto byte_offset = decoder.has_byte_offset()
                               ? std::optional(decoder.byte_offset())
                               : std::nullopt;
  const auto file_object = decoder.has_file_object()
                               ? std::optional(decoder.file_object())
                               : std::nullopt;
  const auto response_time = decoder.has_response_time()
                                 ? std::optional(decoder.response_time())
                                 : std::nullopt;
  const auto issuing_thread_id =
      decoder.has_issuing_thread_id()
          ? std::optional(decoder.issuing_thread_id())
          : std::nullopt;
  std::function<void(ArgsTracker::BoundInserter*)> set_args =
      [this, disk_number, irp_flags, transfer_size, byte_offset, file_object,
       irp, response_time,
       issuing_thread_id](ArgsTracker::BoundInserter* inserter) {
        if (irp) {
          inserter->AddArg(irp_ptr_arg_, Variadic::Pointer(*irp));
        }
        if (disk_number) {
          inserter->AddArg(disk_number_arg_,
                           Variadic::UnsignedInteger(*disk_number));
        }
        if (irp_flags) {
          inserter->AddArg(irp_flags_arg_, Variadic::Pointer(*irp_flags));
        }
        if (transfer_size) {
          inserter->AddArg(transfer_size_arg_,
                           Variadic::UnsignedInteger(*transfer_size));
        }
        if (byte_offset) {
          inserter->AddArg(byte_offset_arg_, Variadic::Integer(*byte_offset));
        }
        if (file_object) {
          inserter->AddArg(file_object_arg_, Variadic::Pointer(*file_object));
        }
        if (response_time) {
          inserter->AddArg(response_time_arg_,
                           Variadic::Integer(*response_time));
        }
        if (issuing_thread_id) {
          inserter->AddArg(issuing_thread_id_arg_,
                           Variadic::UnsignedInteger(*issuing_thread_id));
        }
      };

  const char* event_type = GetEventTypeString(opcode);
  if (!event_type) {
    context_->stats_tracker->IncrementStats(stats::missing_disk_io_event_name);
    return;
  }

  const auto track_id = context_->track_compressor->InternScoped(
      kBlueprint, tracks::Dimensions(utid), timestamp,
      response_time.value_or(0));

  context_->slice_tracker->Scoped(
      timestamp, track_id, context_->storage->InternString(kCategory),
      context_->storage->InternString(event_type), response_time.value_or(0),
      std::move(set_args));
}

}  // namespace perfetto::trace_processor
