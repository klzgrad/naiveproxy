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

#include "src/trace_processor/importers/ftrace/drm_tracker.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/common/flow_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/track_tables_py.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/trace/ftrace/dma_fence.pbzero.h"
#include "protos/perfetto/trace/ftrace/drm.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/gpu_scheduler.pbzero.h"

namespace perfetto::trace_processor {

namespace {

// There are meta-fences such as fence arrays or fence chains where a fence is
// a container of other fences.  These fences are on "unbound" timelines which
// are often dynamically created.  We want to ignore these timelines to avoid
// having tons of tracks for them.
constexpr char kUnboundFenceTimeline[] = "unbound";

constexpr auto kVblankBlueprint = tracks::SliceBlueprint(
    "drm_vblank",
    tracks::DimensionBlueprints(tracks::UintDimensionBlueprint("drm_crtc")),
    tracks::FnNameBlueprint([](uint32_t crtc) {
      return base::StackString<256>("vblank-%u", crtc);
    }));

constexpr auto kSchedRingBlueprint = tracks::SliceBlueprint(
    "drm_sched_ring",
    tracks::DimensionBlueprints(tracks::kNameFromTraceDimensionBlueprint),
    tracks::FnNameBlueprint([](base::StringView name) {
      return base::StackString<256>("sched-%.*s", int(name.size()),
                                    name.data());
    }));

constexpr auto kFenceBlueprint = tracks::SliceBlueprint(
    "drm_fence",
    tracks::DimensionBlueprints(tracks::kNameFromTraceDimensionBlueprint,
                                tracks::UintDimensionBlueprint("context")),
    tracks::FnNameBlueprint([](base::StringView name, uint32_t context) {
      return base::StackString<256>("fence-%.*s-%u", int(name.size()),
                                    name.data(), context);
    }));

}  // namespace

DrmTracker::DrmTracker(TraceProcessorContext* context)
    : context_(context),
      vblank_slice_signal_id_(context->storage->InternString("signal")),
      vblank_slice_deliver_id_(context->storage->InternString("deliver")),
      vblank_arg_seqno_id_(context->storage->InternString("vblank seqno")),
      sched_slice_queue_id_(
          context->storage->InternString("drm_sched_job_queue")),
      sched_slice_job_id_(context->storage->InternString("job")),
      sched_arg_ring_id_(context->storage->InternString("gpu sched ring")),
      sched_arg_job_id_(context->storage->InternString("gpu sched job")),
      fence_slice_fence_id_(context->storage->InternString("fence")),
      fence_slice_wait_id_(context->storage->InternString("dma_fence_wait")),
      fence_arg_context_id_(context->storage->InternString("fence context")),
      fence_arg_seqno_id_(context->storage->InternString("fence seqno")) {}

void DrmTracker::ParseDrm(int64_t timestamp,
                          uint32_t field_id,
                          uint32_t pid,
                          protozero::ConstBytes blob) {
  using protos::pbzero::FtraceEvent;

  switch (field_id) {
    case FtraceEvent::kDrmVblankEventFieldNumber: {
      protos::pbzero::DrmVblankEventFtraceEvent::Decoder evt(blob);
      DrmVblankEvent(timestamp, evt.crtc(), evt.seq());
      break;
    }
    case FtraceEvent::kDrmVblankEventDeliveredFieldNumber: {
      protos::pbzero::DrmVblankEventDeliveredFtraceEvent::Decoder evt(blob);
      DrmVblankEventDelivered(timestamp, evt.crtc(), evt.seq());
      break;
    }
    case FtraceEvent::kDrmSchedJobFieldNumber: {
      protos::pbzero::DrmSchedJobFtraceEvent::Decoder evt(blob);
      SchedJob job = SchedJob::WithLocalId(evt.id());
      DrmSchedJobQueue(timestamp, pid, evt.name(), job);
      break;
    }
    case FtraceEvent::kDrmRunJobFieldNumber: {
      protos::pbzero::DrmRunJobFtraceEvent::Decoder evt(blob);
      SchedJob job = SchedJob::WithGlobalAndLocalId(evt.fence(), evt.id());
      DrmSchedJobRun(timestamp, evt.name(), job);
      break;
    }
    case FtraceEvent::kDrmSchedProcessJobFieldNumber: {
      protos::pbzero::DrmSchedProcessJobFtraceEvent::Decoder evt(blob);
      SchedJob job = SchedJob::WithGlobalId(evt.fence());
      DrmSchedJobDone(timestamp, job);
      break;
    }
    case FtraceEvent::kDmaFenceInitFieldNumber: {
      protos::pbzero::DmaFenceInitFtraceEvent::Decoder evt(blob);
      DmaFenceInit(timestamp, evt.timeline(), evt.context(), evt.seqno());
      break;
    }
    case FtraceEvent::kDmaFenceEmitFieldNumber: {
      protos::pbzero::DmaFenceEmitFtraceEvent::Decoder evt(blob);
      DmaFenceEmit(timestamp, evt.timeline(), evt.context(), evt.seqno());
      break;
    }
    case FtraceEvent::kDmaFenceSignaledFieldNumber: {
      protos::pbzero::DmaFenceSignaledFtraceEvent::Decoder evt(blob);
      DmaFenceSignaled(timestamp, evt.timeline(), evt.context(), evt.seqno());
      break;
    }
    case FtraceEvent::kDmaFenceWaitStartFieldNumber: {
      protos::pbzero::DmaFenceWaitStartFtraceEvent::Decoder evt(blob);
      DmaFenceWaitStart(timestamp, pid, evt.context(), evt.seqno());
      break;
    }
    case FtraceEvent::kDmaFenceWaitEndFieldNumber: {
      DmaFenceWaitEnd(timestamp, pid);
      break;
    }
    case FtraceEvent::kDrmSchedJobDoneFieldNumber: {
      protos::pbzero::DrmSchedJobDoneFtraceEvent::Decoder evt(blob);
      SchedJob job =
          SchedJob::WithFenceId(evt.fence_context(), evt.fence_seqno());
      DrmSchedJobDone(timestamp, job);
      break;
    }
    case FtraceEvent::kDrmSchedJobQueueFieldNumber: {
      protos::pbzero::DrmSchedJobQueueFtraceEvent::Decoder evt(blob);
      SchedJob job =
          SchedJob::WithFenceId(evt.fence_context(), evt.fence_seqno());
      DrmSchedJobQueue(timestamp, pid, evt.name(), job);
      break;
    }
    case FtraceEvent::kDrmSchedJobRunFieldNumber: {
      protos::pbzero::DrmSchedJobRunFtraceEvent::Decoder evt(blob);
      SchedJob job =
          SchedJob::WithFenceId(evt.fence_context(), evt.fence_seqno());
      DrmSchedJobRun(timestamp, evt.name(), job);
      break;
    }
    default:
      PERFETTO_DFATAL("Unexpected field id");
      break;
  }
}

void DrmTracker::DrmVblankEvent(int64_t timestamp,
                                int32_t crtc,
                                uint32_t seqno) {
  TrackId track_id = context_->track_tracker->InternTrack(
      kVblankBlueprint, tracks::Dimensions(crtc));
  context_->slice_tracker->Scoped(
      timestamp, track_id, kNullStringId, vblank_slice_signal_id_, 0,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(vblank_arg_seqno_id_,
                         Variadic::UnsignedInteger(seqno));
      });
}

void DrmTracker::DrmVblankEventDelivered(int64_t timestamp,
                                         int32_t crtc,
                                         uint32_t seqno) {
  TrackId track_id = context_->track_tracker->InternTrack(
      kVblankBlueprint, tracks::Dimensions(crtc));
  context_->slice_tracker->Scoped(
      timestamp, track_id, kNullStringId, vblank_slice_deliver_id_, 0,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(vblank_arg_seqno_id_,
                         Variadic::UnsignedInteger(seqno));
      });
}

DrmTracker::SchedRing& DrmTracker::GetSchedRingByName(base::StringView name) {
  auto* iter = sched_rings_.Find(name);
  if (iter)
    return **iter;

  auto ring = std::make_unique<SchedRing>();
  ring->track_id = context_->track_tracker->InternTrack(
      kSchedRingBlueprint, tracks::Dimensions(name));

  SchedRing& ret = *ring;
  sched_rings_.Insert(name, std::move(ring));

  return ret;
}

void DrmTracker::InsertSchedJobArgs(ArgsTracker::BoundInserter* inserter,
                                    SchedJob job) const {
  uint64_t context, seqno;
  if (job.FenceId(&context, &seqno)) {
    inserter->AddArg(fence_arg_context_id_, Variadic::UnsignedInteger(context));
    inserter->AddArg(fence_arg_seqno_id_, Variadic::UnsignedInteger(seqno));
  } else {
    inserter->AddArg(sched_arg_job_id_,
                     Variadic::UnsignedInteger(job.LocalId()));
  }
}

void DrmTracker::BeginSchedRingSlice(int64_t timestamp, SchedRing& ring) {
  PERFETTO_DCHECK(!ring.running_jobs.empty());
  SchedJob job = ring.running_jobs.front();

  auto args_inserter = [this, job](ArgsTracker::BoundInserter* inserter) {
    InsertSchedJobArgs(inserter, job);
  };

  std::optional<SliceId> slice_id =
      context_->slice_tracker->Begin(timestamp, ring.track_id, kNullStringId,
                                     sched_slice_job_id_, args_inserter);

  if (slice_id) {
    SliceId* out_slice_id = ring.out_slice_ids.Find(job);
    if (out_slice_id) {
      context_->flow_tracker->InsertFlow(*out_slice_id, *slice_id);
      ring.out_slice_ids.Erase(job);
    }
  }
}

void DrmTracker::DrmSchedJobQueue(int64_t timestamp,
                                  uint32_t pid,
                                  base::StringView name,
                                  SchedJob job) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  StringId ring_id = context_->storage->InternString(name);

  std::optional<SliceId> slice_id = context_->slice_tracker->Scoped(
      timestamp, track_id, kNullStringId, sched_slice_queue_id_, 0,
      [&, this](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(sched_arg_ring_id_, Variadic::String(ring_id));
        InsertSchedJobArgs(inserter, job);
      });
  if (slice_id) {
    SchedRing& ring = GetSchedRingByName(name);
    ring.out_slice_ids[job] = *slice_id;
  }
}

void DrmTracker::DrmSchedJobRun(int64_t timestamp,
                                base::StringView name,
                                SchedJob job) {
  SchedRing& ring = GetSchedRingByName(name);

  ring.running_jobs.push_back(job);
  sched_busy_rings_.Insert(job, &ring);

  if (ring.running_jobs.size() == 1)
    BeginSchedRingSlice(timestamp, ring);
}

void DrmTracker::DrmSchedJobDone(int64_t timestamp, SchedJob job) {
  auto* iter = sched_busy_rings_.Find(job);
  if (!iter)
    return;
  SchedRing& ring = **iter;
  sched_busy_rings_.Erase(job);

  ring.running_jobs.pop_front();
  context_->slice_tracker->End(timestamp, ring.track_id);

  if (!ring.running_jobs.empty())
    BeginSchedRingSlice(timestamp, ring);
}

DrmTracker::FenceTimeline& DrmTracker::GetFenceTimelineByContext(
    uint32_t context,
    base::StringView name) {
  auto* iter = fence_timelines_.Find(context);
  if (iter)
    return **iter;

  auto timeline = std::make_unique<FenceTimeline>();
  timeline->track_id = context_->track_tracker->InternTrack(
      kFenceBlueprint, tracks::Dimensions(name, context));

  FenceTimeline& ret = *timeline;
  fence_timelines_.Insert(context, std::move(timeline));

  return ret;
}

void DrmTracker::BeginFenceTimelineSlice(int64_t timestamp,
                                         const FenceTimeline& timeline) {
  PERFETTO_DCHECK(!timeline.pending_fences.empty());
  uint32_t seqno = timeline.pending_fences.front();

  auto args_inserter = [this, seqno](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(fence_arg_seqno_id_, Variadic::UnsignedInteger(seqno));
  };

  context_->slice_tracker->Begin(timestamp, timeline.track_id, kNullStringId,
                                 fence_slice_fence_id_, args_inserter);
}

void DrmTracker::DmaFenceInit(int64_t timestamp,
                              base::StringView name,
                              uint32_t context,
                              uint32_t seqno) {
  if (name == kUnboundFenceTimeline)
    return;

  FenceTimeline& timeline = GetFenceTimelineByContext(context, name);
  // ignore dma_fence_init when the timeline has dma_fence_emit
  if (timeline.has_dma_fence_emit)
    return;

  timeline.pending_fences.push_back(seqno);

  if (timeline.pending_fences.size() == 1)
    BeginFenceTimelineSlice(timestamp, timeline);
}

void DrmTracker::DmaFenceEmit(int64_t timestamp,
                              base::StringView name,
                              uint32_t context,
                              uint32_t seqno) {
  if (name == kUnboundFenceTimeline)
    return;

  FenceTimeline& timeline = GetFenceTimelineByContext(context, name);

  // Most timelines do not have dma_fence_emit and we rely on the less
  // accurate dma_fence_init instead.  But for those who do, we will switch to
  // dma_fence_emit.
  if (!timeline.has_dma_fence_emit) {
    timeline.has_dma_fence_emit = true;

    if (!timeline.pending_fences.empty()) {
      context_->slice_tracker->End(timestamp, timeline.track_id);
      timeline.pending_fences.clear();
    }
  }

  timeline.pending_fences.push_back(seqno);

  if (timeline.pending_fences.size() == 1)
    BeginFenceTimelineSlice(timestamp, timeline);
}

void DrmTracker::DmaFenceSignaled(int64_t timestamp,
                                  base::StringView name,
                                  uint32_t context,
                                  uint32_t seqno) {
  if (name == kUnboundFenceTimeline)
    return;

  FenceTimeline& timeline = GetFenceTimelineByContext(context, name);
  if (timeline.pending_fences.empty() ||
      seqno < timeline.pending_fences.front()) {
    return;
  }

  timeline.pending_fences.pop_front();
  context_->slice_tracker->End(timestamp, timeline.track_id);

  if (!timeline.pending_fences.empty())
    BeginFenceTimelineSlice(timestamp, timeline);
}

void DrmTracker::DmaFenceWaitStart(int64_t timestamp,
                                   uint32_t pid,
                                   uint32_t context,
                                   uint32_t seqno) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  auto args_inserter = [this, context,
                        seqno](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(fence_arg_context_id_, Variadic::UnsignedInteger(context));
    inserter->AddArg(fence_arg_seqno_id_, Variadic::UnsignedInteger(seqno));
  };

  context_->slice_tracker->Begin(timestamp, track_id, kNullStringId,
                                 fence_slice_wait_id_, args_inserter);
}

void DrmTracker::DmaFenceWaitEnd(int64_t timestamp, uint32_t pid) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);

  context_->slice_tracker->End(timestamp, track_id);
}

}  // namespace perfetto::trace_processor
