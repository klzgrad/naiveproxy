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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_FTRACE_DRM_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_FTRACE_DRM_TRACKER_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Tracker for DRM-related events, including vblanks, gpu schedulers, and
// dma-fences.
class DrmTracker {
 public:
  explicit DrmTracker(TraceProcessorContext*);

  void ParseDrm(int64_t timestamp,
                uint32_t field_id,
                uint32_t pid,
                protozero::ConstBytes blob);

 private:
  void DrmVblankEvent(int64_t timestamp, int32_t crtc, uint32_t seqno);
  void DrmVblankEventDelivered(int64_t timestamp, int32_t crtc, uint32_t seqno);

  // A SchedJob represents a scheduler job.
  //
  // Since linux 6.17, a job is always identified by a fence id (dma-fence
  // context and seqno).
  //
  // Before linux 6.17, a job is identified by
  //
  //  - a local id (local to the ring) in DrmSchedJobFtraceEvent,
  //  - a global id (dma-fence addr) in DrmSchedProcessJobFtraceEvent, and
  //  - both local and global id in DrmRunJobFtraceEvent.
  class SchedJob {
   public:
    static SchedJob WithFenceId(uint64_t context, uint64_t seqno) {
      return SchedJob(SCHED_JOB_ID_FENCE, context, seqno);
    }

    static SchedJob WithGlobalAndLocalId(uint64_t global_id,
                                         uint64_t local_id) {
      return SchedJob(SCHED_JOB_ID_GLOBAL | SCHED_JOB_ID_LOCAL, global_id,
                      local_id);
    }

    static SchedJob WithGlobalId(uint64_t global_id) {
      return SchedJob(SCHED_JOB_ID_GLOBAL, global_id, 0);
    }

    static SchedJob WithLocalId(uint64_t local_id) {
      return SchedJob(SCHED_JOB_ID_LOCAL, 0, local_id);
    }

    bool FenceId(uint64_t* context, uint64_t* seqno) const {
      if (types_ & SCHED_JOB_ID_FENCE) {
        *context = id_[0];
        *seqno = id_[1];
        return true;
      } else {
        *context = 0;
        *seqno = 0;
        return false;
      }
    }

    uint64_t GlobalId() const {
      return types_ & SCHED_JOB_ID_GLOBAL ? id_[0] : 0;
    }

    uint64_t LocalId() const {
      return types_ & SCHED_JOB_ID_LOCAL ? id_[1] : 0;
    }

    bool operator==(const SchedJob& other) const {
      if (types_ & other.types_ & SCHED_JOB_ID_FENCE)
        return id_[0] == other.id_[0] && id_[1] == other.id_[1];

      if (types_ & other.types_ & SCHED_JOB_ID_GLOBAL)
        return id_[0] == other.id_[0];

      // Assume the two jobs are on the same ring.
      if (types_ & other.types_ & SCHED_JOB_ID_LOCAL)
        return id_[1] == other.id_[1];

      return false;
    }

    // Hash the global id (before 6.17) or the fence id (since 6.17).
    struct GlobalHash {
      size_t operator()(const SchedJob& job) const {
        uint64_t context;
        uint64_t seqno;
        return job.FenceId(&context, &seqno)
                   ? base::MurmurHashCombine(context, seqno)
                   : base::MurmurHashValue(job.GlobalId());
      }
    };

    // Hash the local id (before 6.17) or the fence id (since 6.17).
    struct LocalHash {
      size_t operator()(const SchedJob& job) const {
        uint64_t context;
        uint64_t seqno;
        return job.FenceId(&context, &seqno)
                   ? base::MurmurHashCombine(context, seqno)
                   : base::MurmurHashValue(job.LocalId());
      }
    };

   private:
    enum SchedJobIdType : uint8_t {
      // dma-fence context in id_[0] and dma-fence seqno id_[1]
      SCHED_JOB_ID_FENCE = 1 << 0,
      // Global id in id_[0].
      SCHED_JOB_ID_GLOBAL = 1 << 1,
      // Local id in id_[1].
      SCHED_JOB_ID_LOCAL = 1 << 2,
    };

    SchedJob(uint8_t types, uint64_t id0, uint64_t id1)
        : types_(types), id_{id0, id1} {}

    // Since 6.17, always SCHED_JOB_ID_FENCE.
    // Before 6.17, a bitmask of SCHED_JOB_ID_GLOBAL and SCHED_JOB_ID_LOCAL.
    uint8_t types_;

    uint64_t id_[2];
  };

  // A SchedRing represents a scheduler ring buffer.
  struct SchedRing {
    TrackId track_id;

    // Jobs that are running and have yet done.
    std::deque<SchedJob> running_jobs;

    // Map queued jobs to their slice ids on the thread track.
    base::FlatHashMap<SchedJob, SliceId, SchedJob::LocalHash> out_slice_ids;
  };

  SchedRing& GetSchedRingByName(base::StringView name);
  void InsertSchedJobArgs(ArgsTracker::BoundInserter* inserter,
                          SchedJob job) const;
  void BeginSchedRingSlice(int64_t timestamp, SchedRing& ring);

  void DrmSchedJobQueue(int64_t timestamp,
                        uint32_t pid,
                        base::StringView name,
                        SchedJob job);
  void DrmSchedJobRun(int64_t timestamp, base::StringView name, SchedJob job);
  void DrmSchedJobDone(int64_t timestamp, SchedJob job);

  // A FenceTimeline represents a dma-fence context.
  struct FenceTimeline {
    TrackId track_id;
    bool has_dma_fence_emit;

    // dma-fences that are initialized and have yet signaled.
    std::deque<uint32_t> pending_fences;
  };
  FenceTimeline& GetFenceTimelineByContext(uint32_t context,
                                           base::StringView name);
  void BeginFenceTimelineSlice(int64_t timestamp,
                               const FenceTimeline& timeline);

  void DmaFenceInit(int64_t timestamp,
                    base::StringView name,
                    uint32_t context,
                    uint32_t seqno);
  void DmaFenceEmit(int64_t timestamp,
                    base::StringView name,
                    uint32_t context,
                    uint32_t seqno);
  void DmaFenceSignaled(int64_t timestamp,
                        base::StringView name,
                        uint32_t context,
                        uint32_t seqno);
  void DmaFenceWaitStart(int64_t timestamp,
                         uint32_t pid,
                         uint32_t context,
                         uint32_t seqno);
  void DmaFenceWaitEnd(int64_t timestamp, uint32_t pid);

  TraceProcessorContext* const context_;

  const StringId vblank_slice_signal_id_;
  const StringId vblank_slice_deliver_id_;
  const StringId vblank_arg_seqno_id_;
  const StringId sched_slice_queue_id_;
  const StringId sched_slice_job_id_;
  const StringId sched_arg_ring_id_;
  const StringId sched_arg_job_id_;
  const StringId fence_slice_fence_id_;
  const StringId fence_slice_wait_id_;
  const StringId fence_arg_context_id_;
  const StringId fence_arg_seqno_id_;

  // Map scheduler ring names to SchedRings.
  base::FlatHashMap<base::StringView, std::unique_ptr<SchedRing>> sched_rings_;
  // Map running jobs to SchedRings.
  base::FlatHashMap<SchedJob, SchedRing*, SchedJob::GlobalHash>
      sched_busy_rings_;

  // Map dma-fence contexts to FenceTimelines.
  base::FlatHashMap<uint32_t, std::unique_ptr<FenceTimeline>> fence_timelines_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_FTRACE_DRM_TRACKER_H_
