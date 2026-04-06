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

#include "src/trace_processor/importers/ftrace/virtio_gpu_tracker.h"

#include <cstdint>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/virtio_gpu.pbzero.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/storage/trace_storage.h"

enum virtio_gpu_ctrl_type {
  VIRTIO_GPU_UNDEFINED = 0,

  /* 2d commands */
  VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
  VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
  VIRTIO_GPU_CMD_RESOURCE_UNREF,
  VIRTIO_GPU_CMD_SET_SCANOUT,
  VIRTIO_GPU_CMD_RESOURCE_FLUSH,
  VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
  VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
  VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
  VIRTIO_GPU_CMD_GET_CAPSET_INFO,
  VIRTIO_GPU_CMD_GET_CAPSET,
  VIRTIO_GPU_CMD_GET_EDID,
  VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID,
  VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB,
  VIRTIO_GPU_CMD_SET_SCANOUT_BLOB,

  /* 3d commands */
  VIRTIO_GPU_CMD_CTX_CREATE = 0x0200,
  VIRTIO_GPU_CMD_CTX_DESTROY,
  VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
  VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
  VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
  VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
  VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
  VIRTIO_GPU_CMD_SUBMIT_3D,
  VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB,
  VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB,

  /* cursor commands */
  VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
  VIRTIO_GPU_CMD_MOVE_CURSOR,

  /* success responses */
  VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
  VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
  VIRTIO_GPU_RESP_OK_CAPSET_INFO,
  VIRTIO_GPU_RESP_OK_CAPSET,
  VIRTIO_GPU_RESP_OK_EDID,
  VIRTIO_GPU_RESP_OK_RESOURCE_UUID,
  VIRTIO_GPU_RESP_OK_MAP_INFO,

  /* error responses */
  VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
  VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
  VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
  VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
  VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
  VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};

static const char* virtio_gpu_ctrl_name(uint32_t type) {
  switch (type) {
#define ENUM(n)            \
  case VIRTIO_GPU_CMD_##n: \
    return #n
    /* 2d commands */
    ENUM(GET_DISPLAY_INFO);
    ENUM(RESOURCE_CREATE_2D);
    ENUM(RESOURCE_UNREF);
    ENUM(SET_SCANOUT);
    ENUM(RESOURCE_FLUSH);
    ENUM(TRANSFER_TO_HOST_2D);
    ENUM(RESOURCE_ATTACH_BACKING);
    ENUM(RESOURCE_DETACH_BACKING);
    ENUM(GET_CAPSET_INFO);
    ENUM(GET_CAPSET);
    ENUM(GET_EDID);
    ENUM(RESOURCE_ASSIGN_UUID);
    ENUM(RESOURCE_CREATE_BLOB);
    ENUM(SET_SCANOUT_BLOB);

    /* 3d commands */
    ENUM(CTX_CREATE);
    ENUM(CTX_DESTROY);
    ENUM(CTX_ATTACH_RESOURCE);
    ENUM(CTX_DETACH_RESOURCE);
    ENUM(RESOURCE_CREATE_3D);
    ENUM(TRANSFER_TO_HOST_3D);
    ENUM(TRANSFER_FROM_HOST_3D);
    ENUM(SUBMIT_3D);
    ENUM(RESOURCE_MAP_BLOB);
    ENUM(RESOURCE_UNMAP_BLOB);

    /* cursor commands */
    ENUM(UPDATE_CURSOR);
    ENUM(MOVE_CURSOR);
#undef ENUM
    default:
      return "";
  }
}

namespace perfetto::trace_processor {

namespace {

constexpr auto kVirtgpuNameDimension =
    tracks::StringDimensionBlueprint("virtgpu_name");

constexpr auto kQueueBlueprint = TrackCompressor::SliceBlueprint(
    "virtgpu_queue_event",
    tracks::Dimensions(kVirtgpuNameDimension),
    tracks::FnNameBlueprint([](base::StringView name) {
      return base::StackString<255>("Virtgpu %.*s Queue", int(name.size()),
                                    name.data());
    }));

}  // namespace

VirtioGpuTracker::VirtioGpuTracker(TraceProcessorContext* context)
    : virtgpu_control_queue_(context, "Control"),
      virtgpu_cursor_queue_(context, "Cursor") {}

void VirtioGpuTracker::ParseVirtioGpu(int64_t timestamp,
                                      uint32_t field_id,
                                      uint32_t pid,
                                      protozero::ConstBytes blob) {
  using protos::pbzero::FtraceEvent;

  switch (field_id) {
    case FtraceEvent::kVirtioGpuCmdQueueFieldNumber: {
      ParseVirtioGpuCmdQueue(timestamp, pid, blob);
      break;
    }
    case FtraceEvent::kVirtioGpuCmdResponseFieldNumber: {
      ParseVirtioGpuCmdResponse(timestamp, pid, blob);
      break;
    }
    default:
      PERFETTO_DFATAL("Unexpected field id");
      break;
  }
}

VirtioGpuTracker::VirtioGpuQueue::VirtioGpuQueue(TraceProcessorContext* context,
                                                 const char* name)
    : context_(context), name_(name) {}

void VirtioGpuTracker::VirtioGpuQueue::HandleNumFree(int64_t timestamp,
                                                     uint32_t num_free) {
  static constexpr auto kBlueprint = tracks::CounterBlueprint(
      "virtgpu_num_free", tracks::UnknownUnitBlueprint(),
      tracks::DimensionBlueprints(kVirtgpuNameDimension),
      tracks::FnNameBlueprint([](base::StringView name) {
        return base::StackString<255>("Virtgpu %.*s Free", int(name.size()),
                                      name.data());
      }));

  TrackId track = context_->track_tracker->InternTrack(
      kBlueprint, tracks::Dimensions(name_));
  context_->event_tracker->PushCounter(timestamp, static_cast<double>(num_free),
                                       track);
}

void VirtioGpuTracker::VirtioGpuQueue::HandleCmdQueue(int64_t timestamp,
                                                      uint32_t seqno,
                                                      uint32_t type,
                                                      uint64_t fence_id) {
  TrackId start_id = context_->track_compressor->InternBegin(
      kQueueBlueprint, tracks::Dimensions(name_), seqno);
  context_->slice_tracker->Begin(
      timestamp, start_id, kNullStringId,
      context_->storage->InternString(
          base::StringView(virtio_gpu_ctrl_name(type))));

  /* cmds with a fence do not necessarily get an immediate response from
   * the host, so we should not use them for calculating latency:
   */
  if (!fence_id) {
    start_timestamps_[seqno] = timestamp;
  }
}

void VirtioGpuTracker::VirtioGpuQueue::HandleCmdResponse(int64_t timestamp,
                                                         uint32_t seqno) {
  TrackId end_id = context_->track_compressor->InternEnd(
      kQueueBlueprint, tracks::Dimensions(name_), seqno);
  context_->slice_tracker->End(timestamp, end_id);

  int64_t* start_timestamp = start_timestamps_.Find(seqno);
  if (!start_timestamp) {
    return;
  }

  int64_t duration = timestamp - *start_timestamp;

  static constexpr auto kBlueprint = tracks::CounterBlueprint(
      "virtgpu_latency", tracks::UnknownUnitBlueprint(),
      tracks::DimensionBlueprints(kVirtgpuNameDimension),
      tracks::FnNameBlueprint([](base::StringView name) {
        return base::StackString<255>("Virtgpu %.*s Latency", int(name.size()),
                                      name.data());
      }));

  TrackId track = context_->track_tracker->InternTrack(
      kBlueprint, tracks::Dimensions(name_));
  context_->event_tracker->PushCounter(timestamp, static_cast<double>(duration),
                                       track);
  start_timestamps_.Erase(seqno);
}

void VirtioGpuTracker::ParseVirtioGpuCmdQueue(int64_t timestamp,
                                              uint32_t /*pid*/,
                                              protozero::ConstBytes blob) {
  protos::pbzero::VirtioGpuCmdQueueFtraceEvent::Decoder evt(blob);

  auto name = base::StringView(evt.name());
  if (name == "control") {
    virtgpu_control_queue_.HandleNumFree(timestamp, evt.num_free());
    virtgpu_control_queue_.HandleCmdQueue(timestamp, evt.seqno(), evt.type(),
                                          evt.fence_id());
  } else if (name == "cursor") {
    virtgpu_cursor_queue_.HandleNumFree(timestamp, evt.num_free());
    virtgpu_cursor_queue_.HandleCmdQueue(timestamp, evt.seqno(), evt.type(),
                                         evt.fence_id());
  }
}

void VirtioGpuTracker::ParseVirtioGpuCmdResponse(int64_t timestamp,
                                                 uint32_t /*pid*/,
                                                 protozero::ConstBytes blob) {
  protos::pbzero::VirtioGpuCmdResponseFtraceEvent::Decoder evt(blob);
  auto name = base::StringView(evt.name());
  if (name == "control") {
    virtgpu_control_queue_.HandleNumFree(timestamp, evt.num_free());
    virtgpu_control_queue_.HandleCmdResponse(timestamp, evt.seqno());
  } else if (name == "cursor") {
    virtgpu_cursor_queue_.HandleNumFree(timestamp, evt.num_free());
    virtgpu_cursor_queue_.HandleCmdResponse(timestamp, evt.seqno());
  }
}

}  // namespace perfetto::trace_processor
