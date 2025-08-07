/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "src/tracing/core/shared_memory_arbiter_impl.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/tracing/core/commit_data_request.h"
#include "perfetto/ext/tracing/core/shared_memory.h"
#include "perfetto/ext/tracing/core/shared_memory_abi.h"
#include "src/tracing/core/null_trace_writer.h"
#include "src/tracing/core/trace_writer_impl.h"

namespace perfetto {

using Chunk = SharedMemoryABI::Chunk;

namespace {
static_assert(sizeof(BufferID) == sizeof(uint16_t),
              "The MaybeUnboundBufferID logic requires BufferID not to grow "
              "above uint16_t.");

MaybeUnboundBufferID MakeTargetBufferIdForReservation(uint16_t reservation_id) {
  // Reservation IDs are stored in the upper bits.
  PERFETTO_CHECK(reservation_id > 0);
  return static_cast<MaybeUnboundBufferID>(reservation_id) << 16;
}

bool IsReservationTargetBufferId(MaybeUnboundBufferID buffer_id) {
  return (buffer_id >> 16) > 0;
}
}  // namespace

// static
SharedMemoryABI::PageLayout SharedMemoryArbiterImpl::default_page_layout =
    SharedMemoryABI::PageLayout::kPageDiv1;

// static
std::unique_ptr<SharedMemoryArbiter> SharedMemoryArbiter::CreateInstance(
    SharedMemory* shared_memory,
    size_t page_size,
    ShmemMode mode,
    TracingService::ProducerEndpoint* producer_endpoint,
    base::TaskRunner* task_runner) {
  return std::unique_ptr<SharedMemoryArbiterImpl>(new SharedMemoryArbiterImpl(
      shared_memory->start(), shared_memory->size(), mode, page_size,
      producer_endpoint, task_runner));
}

// static
std::unique_ptr<SharedMemoryArbiter> SharedMemoryArbiter::CreateUnboundInstance(
    SharedMemory* shared_memory,
    size_t page_size,
    ShmemMode mode) {
  return std::unique_ptr<SharedMemoryArbiterImpl>(new SharedMemoryArbiterImpl(
      shared_memory->start(), shared_memory->size(), mode, page_size,
      /*producer_endpoint=*/nullptr, /*task_runner=*/nullptr));
}

SharedMemoryArbiterImpl::SharedMemoryArbiterImpl(
    void* start,
    size_t size,
    ShmemMode mode,
    size_t page_size,
    TracingService::ProducerEndpoint* producer_endpoint,
    base::TaskRunner* task_runner)
    : producer_endpoint_(producer_endpoint),
      use_shmem_emulation_(mode == ShmemMode::kShmemEmulation),
      task_runner_(task_runner),
      shmem_abi_(reinterpret_cast<uint8_t*>(start), size, page_size, mode),
      active_writer_ids_(kMaxWriterID),
      fully_bound_(task_runner && producer_endpoint),
      was_always_bound_(fully_bound_),
      weak_ptr_factory_(this) {}

Chunk SharedMemoryArbiterImpl::GetNewChunk(
    const SharedMemoryABI::ChunkHeader& header,
    BufferExhaustedPolicy buffer_exhausted_policy) {
  int stall_count = 0;
  unsigned stall_interval_us = 0;
  bool task_runner_runs_on_current_thread = false;
  static const unsigned kMaxStallIntervalUs = 100000;
  static const int kLogAfterNStalls = 3;
  static const int kFlushCommitsAfterEveryNStalls = 2;
  static const int kAssertAtNStalls = 200;

  bool should_stall = false;
  bool should_abort = false;

  switch (buffer_exhausted_policy) {
    case BufferExhaustedPolicy::kDrop:
      break;
    case BufferExhaustedPolicy::kStall:
      should_stall = true;
      should_abort = true;
      break;
    case BufferExhaustedPolicy::kStallThenDrop:
      should_stall = true;
      should_abort = false;
      break;
  }

  for (;;) {
    // TODO(primiano): Probably this lock is not really required and this code
    // could be rewritten leveraging only the Try* atomic operations in
    // SharedMemoryABI. But let's not be too adventurous for the moment.
    {
      std::unique_lock<std::mutex> scoped_lock(lock_);

      // If ever unbound, we do not support stalling. In theory, we could
      // support stalling for TraceWriters created after the arbiter and startup
      // buffer reservations were bound, but to avoid raciness between the
      // creation of startup writers and binding, we categorically forbid kStall
      // mode.
      PERFETTO_DCHECK(was_always_bound_ || !should_stall);

      task_runner_runs_on_current_thread =
          task_runner_ && task_runner_->RunsTasksOnCurrentThread();

      // If more than half of the SMB.size() is filled with completed chunks for
      // which we haven't notified the service yet (i.e. they are still enqueued
      // in |commit_data_req_|), force a synchronous CommitDataRequest() even if
      // we acquire a chunk, to reduce the likeliness of stalling the writer.
      //
      // We can only do this if we're writing on the same thread that we access
      // the producer endpoint on, since we cannot notify the producer endpoint
      // to commit synchronously on a different thread. Attempting to flush
      // synchronously on another thread will lead to subtle bugs caused by
      // out-of-order commit requests (crbug.com/919187#c28).
      bool should_commit_synchronously =
          task_runner_runs_on_current_thread && should_stall &&
          commit_data_req_ && bytes_pending_commit_ >= shmem_abi_.size() / 2;

      const size_t initial_page_idx = page_idx_;
      for (size_t i = 0; i < shmem_abi_.num_pages(); i++) {
        page_idx_ = (initial_page_idx + i) % shmem_abi_.num_pages();
        bool is_new_page = false;

        // TODO(primiano): make the page layout dynamic.
        auto layout = SharedMemoryArbiterImpl::default_page_layout;

        if (shmem_abi_.is_page_free(page_idx_)) {
          is_new_page = shmem_abi_.TryPartitionPage(page_idx_, layout);
        }
        uint32_t free_chunks;
        if (is_new_page) {
          free_chunks = (1 << SharedMemoryABI::kNumChunksForLayout[layout]) - 1;
        } else {
          free_chunks = shmem_abi_.GetFreeChunks(page_idx_);
        }

        for (uint32_t chunk_idx = 0; free_chunks;
             chunk_idx++, free_chunks >>= 1) {
          if (!(free_chunks & 1))
            continue;
          // We found a free chunk.
          Chunk chunk = shmem_abi_.TryAcquireChunkForWriting(
              page_idx_, chunk_idx, &header);
          if (!chunk.is_valid())
            continue;
          if (stall_count > kLogAfterNStalls) {
            PERFETTO_DLOG("Recovered from stall after %d iterations",
                          stall_count);
          }

          if (should_commit_synchronously) {
            // We can't flush while holding the lock.
            scoped_lock.unlock();
            FlushPendingCommitDataRequests();
            return chunk;
          } else {
            return chunk;
          }
        }
      }
    }  // scoped_lock

    if (!should_stall) {
      PERFETTO_DLOG("Shared memory buffer exhausted, returning invalid Chunk!");
      return Chunk();
    }

    // Stalling is not supported if we were ever unbound (see earlier comment).
    PERFETTO_CHECK(was_always_bound_);

    // All chunks are taken (either kBeingWritten by us or kBeingRead by the
    // Service).
    if (stall_count++ == kLogAfterNStalls) {
      PERFETTO_DLOG("Shared memory buffer overrun! Stalling");
    }

    if (stall_count == kAssertAtNStalls) {
      if (should_abort) {
        Stats stats = GetStats();
        PERFETTO_FATAL(
            "Shared memory buffer max stall count exceeded; possible deadlock "
            "free=%zu bw=%zu br=%zu comp=%zu pages_free=%zu pages_err=%zu",
            stats.chunks_free, stats.chunks_being_written,
            stats.chunks_being_read, stats.chunks_complete, stats.pages_free,
            stats.pages_unexpected);
      } else {
        PERFETTO_DLOG(
            "Shared memory buffer exhausted, returning invalid Chunk!");
        return Chunk();
      }
    }

    // If the IPC thread itself is stalled because the current process has
    // filled up the SMB, we need to make sure that the service can process and
    // purge the chunks written by our process, by flushing any pending commit
    // requests. Because other threads in our process can continue to
    // concurrently grab, fill and commit any chunks purged by the service, it
    // is possible that the SMB remains full and the IPC thread remains stalled,
    // needing to flush the concurrently queued up commits again. This is
    // particularly likely with in-process perfetto service where the IPC thread
    // is the service thread. To avoid remaining stalled forever in such a
    // situation, we attempt to flush periodically after every N stalls.
    if (stall_count % kFlushCommitsAfterEveryNStalls == 0 &&
        task_runner_runs_on_current_thread) {
      // TODO(primiano): sending the IPC synchronously is a temporary workaround
      // until the backpressure logic in probes_producer is sorted out. Until
      // then the risk is that we stall the message loop waiting for the tracing
      // service to consume the shared memory buffer (SMB) and, for this reason,
      // never run the task that tells the service to purge the SMB. This must
      // happen iff we are on the IPC thread, not doing this will cause
      // deadlocks, doing this on the wrong thread causes out-of-order data
      // commits (crbug.com/919187#c28).
      FlushPendingCommitDataRequests();
    } else {
      base::SleepMicroseconds(stall_interval_us);
      stall_interval_us =
          std::min(kMaxStallIntervalUs, (stall_interval_us + 1) * 8);
    }
  }
}

void SharedMemoryArbiterImpl::ReturnCompletedChunk(
    Chunk chunk,
    MaybeUnboundBufferID target_buffer,
    PatchList* patch_list) {
  PERFETTO_DCHECK(chunk.is_valid());
  const WriterID writer_id = chunk.writer_id();
  UpdateCommitDataRequest(std::move(chunk), writer_id, target_buffer,
                          patch_list);
}

void SharedMemoryArbiterImpl::SendPatches(WriterID writer_id,
                                          MaybeUnboundBufferID target_buffer,
                                          PatchList* patch_list) {
  PERFETTO_DCHECK(!patch_list->empty() && patch_list->front().is_patched());
  UpdateCommitDataRequest(Chunk(), writer_id, target_buffer, patch_list);
}

void SharedMemoryArbiterImpl::UpdateCommitDataRequest(
    Chunk chunk,
    WriterID writer_id,
    MaybeUnboundBufferID target_buffer,
    PatchList* patch_list) {
  // Note: chunk will be invalid if the call came from SendPatches().
  base::TaskRunner* task_runner_to_post_delayed_callback_on = nullptr;
  // The delay with which the flush will be posted.
  uint32_t flush_delay_ms = 0;
  base::WeakPtr<SharedMemoryArbiterImpl> weak_this;
  {
    std::unique_lock<std::mutex> scoped_lock(lock_);

    if (!commit_data_req_) {
      commit_data_req_.reset(new CommitDataRequest());

      // Flushing the commit is only supported while we're |fully_bound_|. If we
      // aren't, we'll flush when |fully_bound_| is updated.
      if (fully_bound_ && !delayed_flush_scheduled_) {
        weak_this = weak_ptr_factory_.GetWeakPtr();
        task_runner_to_post_delayed_callback_on = task_runner_;
        flush_delay_ms = batch_commits_duration_ms_;
        delayed_flush_scheduled_ = true;
      }
    }

    CommitDataRequest::ChunksToMove* ctm = nullptr;  // Set if chunk is valid.
    // If a valid chunk is specified, return it and attach it to the request.
    if (chunk.is_valid()) {
      PERFETTO_DCHECK(chunk.writer_id() == writer_id);
      uint8_t chunk_idx = chunk.chunk_idx();
      bytes_pending_commit_ += chunk.size();
      size_t page_idx;

      ctm = commit_data_req_->add_chunks_to_move();
      // If the chunk needs patching, it should not be marked as complete yet,
      // because this would indicate to the service that the producer will not
      // be writing to it anymore, while the producer might still apply patches
      // to the chunk later on. In particular, when re-reading (e.g. because of
      // periodic scraping) a completed chunk, the service expects the flags of
      // that chunk not to be removed between reads. So, let's say the producer
      // marked the chunk as complete here and the service then read it for the
      // first time. If the producer then fully patched the chunk, thus removing
      // the kChunkNeedsPatching flag, and the service re-read the chunk after
      // the patching, the service would be thrown off by the removed flag.
      if (direct_patching_enabled_ &&
          (chunk.GetPacketCountAndFlags().second &
           SharedMemoryABI::ChunkHeader::kChunkNeedsPatching)) {
        page_idx = shmem_abi_.GetPageAndChunkIndex(std::move(chunk)).first;
      } else {
        // If the chunk doesn't need patching, we can mark it as complete
        // immediately. This allows the service to read it in full while
        // scraping, which would not be the case if the chunk was left in a
        // kChunkBeingWritten state.
        page_idx = shmem_abi_.ReleaseChunkAsComplete(std::move(chunk));
      }

      // DO NOT access |chunk| after this point, it has been std::move()-d
      // above.
      ctm->set_page(static_cast<uint32_t>(page_idx));
      ctm->set_chunk(chunk_idx);
      ctm->set_target_buffer(target_buffer);
    }

    // Process the completed patches for previous chunks from the |patch_list|.
    CommitDataRequest::ChunkToPatch* last_patch_req = nullptr;
    while (!patch_list->empty() && patch_list->front().is_patched()) {
      Patch curr_patch = patch_list->front();
      patch_list->pop_front();
      // Patches for the same chunk are contiguous in the |patch_list|. So, to
      // determine if there are any other patches that apply to the chunk that
      // is being patched, check if the next patch in the |patch_list| applies
      // to the same chunk.
      bool chunk_needs_more_patching =
          !patch_list->empty() &&
          patch_list->front().chunk_id == curr_patch.chunk_id;

      if (direct_patching_enabled_ &&
          TryDirectPatchLocked(writer_id, curr_patch,
                               chunk_needs_more_patching)) {
        continue;
      }

      // The chunk that this patch applies to has already been released to the
      // service, so it cannot be patches here. Add the patch to the commit data
      // request, so that it can be sent to the service and applied there.
      if (!last_patch_req ||
          last_patch_req->chunk_id() != curr_patch.chunk_id) {
        last_patch_req = commit_data_req_->add_chunks_to_patch();
        last_patch_req->set_writer_id(writer_id);
        last_patch_req->set_chunk_id(curr_patch.chunk_id);
        last_patch_req->set_target_buffer(target_buffer);
      }
      auto* patch = last_patch_req->add_patches();
      patch->set_offset(curr_patch.offset);
      patch->set_data(&curr_patch.size_field[0], curr_patch.size_field.size());
    }

    // Patches are enqueued in the |patch_list| in order and are notified to
    // the service when the chunk is returned. The only case when the current
    // patch list is incomplete is if there is an unpatched entry at the head of
    // the |patch_list| that belongs to the same ChunkID as the last one we are
    // about to send to the service.
    if (last_patch_req && !patch_list->empty() &&
        patch_list->front().chunk_id == last_patch_req->chunk_id()) {
      last_patch_req->set_has_more_patches(true);
    }

    // If the buffer is filling up or if we are given a patch for a chunk
    // that was already sent to the service, we don't want to wait for the next
    // delayed flush to happen and we flush immediately. Otherwise, if we
    // accumulate the patch and a crash occurs before the patch is sent, the
    // service will not know of the patch and won't be able to reconstruct the
    // trace.
    if (fully_bound_ &&
        (last_patch_req || bytes_pending_commit_ >= shmem_abi_.size() / 2)) {
      weak_this = weak_ptr_factory_.GetWeakPtr();
      task_runner_to_post_delayed_callback_on = task_runner_;
      flush_delay_ms = 0;
    }

    // When using shmem emulation we commit the completed chunks immediately
    // to prevent the |bytes_pending_commit_| to become greater than the size
    // of the IPC buffer, since the chunk's data must be passed in the commit
    // data request proto through the network socket. Not doing so could
    // result in a "IPC Frame too large" issue on the host traced side.
    if (fully_bound_ && use_shmem_emulation_) {
      if (task_runner_->RunsTasksOnCurrentThread()) {
        task_runner_to_post_delayed_callback_on = nullptr;
        // Allow next call to UpdateCommitDataRequest to start
        // another batching period.
        delayed_flush_scheduled_ = false;
        // We can't flush while holding the lock
        scoped_lock.unlock();
        FlushPendingCommitDataRequests();
      } else {
        // Since we aren't on the |task_runner_| thread post a task instead,
        // in order to prevent non-overlaping commit data request flushes.
        weak_this = weak_ptr_factory_.GetWeakPtr();
        task_runner_to_post_delayed_callback_on = task_runner_;
        flush_delay_ms = 0;
      }
    }
  }  // scoped_lock(lock_)

  // We shouldn't post tasks while locked.
  // |task_runner_to_post_delayed_callback_on| remains valid after unlocking,
  // because |task_runner_| is never reset.
  if (task_runner_to_post_delayed_callback_on) {
    task_runner_to_post_delayed_callback_on->PostDelayedTask(
        [weak_this] {
          if (!weak_this)
            return;
          {
            std::lock_guard<std::mutex> scoped_lock(weak_this->lock_);
            // Clear |delayed_flush_scheduled_|, allowing the next call to
            // UpdateCommitDataRequest to start another batching period.
            weak_this->delayed_flush_scheduled_ = false;
          }
          weak_this->FlushPendingCommitDataRequests();
        },
        flush_delay_ms);
  }
}

bool SharedMemoryArbiterImpl::TryDirectPatchLocked(
    WriterID writer_id,
    const Patch& patch,
    bool chunk_needs_more_patching) {
  // Search the chunks that are being batched in |commit_data_req_| for a chunk
  // that needs patching and that matches the provided |writer_id| and
  // |patch.chunk_id|. Iterate |commit_data_req_| in reverse, since
  // |commit_data_req_| is appended to at the end with newly-returned chunks,
  // and patches are more likely to apply to chunks that have been returned
  // recently.
  SharedMemoryABI::Chunk chunk;
  bool chunk_found = false;
  auto& chunks_to_move = commit_data_req_->chunks_to_move();
  for (auto ctm_it = chunks_to_move.rbegin(); ctm_it != chunks_to_move.rend();
       ++ctm_it) {
    uint32_t header_bitmap = shmem_abi_.GetPageHeaderBitmap(ctm_it->page());
    auto chunk_state = shmem_abi_.GetChunkStateFromHeaderBitmap(
        header_bitmap, ctm_it->chunk());
    // Note: the subset of |commit_data_req_| chunks that still need patching is
    // also the subset of chunks that are still being written to. The rest of
    // the chunks in |commit_data_req_| do not need patching and have already
    // been marked as complete.
    if (chunk_state != SharedMemoryABI::kChunkBeingWritten)
      continue;

    chunk = shmem_abi_.GetChunkUnchecked(ctm_it->page(), header_bitmap,
                                         ctm_it->chunk());
    if (chunk.writer_id() == writer_id &&
        chunk.header()->chunk_id.load(std::memory_order_relaxed) ==
            patch.chunk_id) {
      chunk_found = true;
      break;
    }
  }

  if (!chunk_found) {
    // The chunk has already been committed to the service and the patch cannot
    // be applied in the producer.
    return false;
  }

  // Apply the patch.
  size_t page_idx;
  uint8_t chunk_idx;
  std::tie(page_idx, chunk_idx) = shmem_abi_.GetPageAndChunkIndex(chunk);
  PERFETTO_DCHECK(shmem_abi_.GetChunkState(page_idx, chunk_idx) ==
                  SharedMemoryABI::ChunkState::kChunkBeingWritten);
  auto chunk_begin = chunk.payload_begin();
  uint8_t* ptr = chunk_begin + patch.offset;
  PERFETTO_CHECK(ptr <= chunk.end() - SharedMemoryABI::kPacketHeaderSize);
  // DCHECK that we are writing into a zero-filled size field and not into
  // valid data. It relies on ScatteredStreamWriter::ReserveBytes() to
  // zero-fill reservations in debug builds.
  const char zero[SharedMemoryABI::kPacketHeaderSize]{};
  PERFETTO_DCHECK(memcmp(ptr, &zero, SharedMemoryABI::kPacketHeaderSize) == 0);

  memcpy(ptr, &patch.size_field[0], SharedMemoryABI::kPacketHeaderSize);

  if (!chunk_needs_more_patching) {
    // Mark that the chunk doesn't need more patching and mark it as complete,
    // as the producer will not write to it anymore. This allows the service to
    // read the chunk in full while scraping, which would not be the case if the
    // chunk was left in a kChunkBeingWritten state.
    chunk.ClearNeedsPatchingFlag();
    shmem_abi_.ReleaseChunkAsComplete(std::move(chunk));
  }

  return true;
}

void SharedMemoryArbiterImpl::SetBatchCommitsDuration(
    uint32_t batch_commits_duration_ms) {
  std::lock_guard<std::mutex> scoped_lock(lock_);
  batch_commits_duration_ms_ = batch_commits_duration_ms;
}

bool SharedMemoryArbiterImpl::EnableDirectSMBPatching() {
  std::lock_guard<std::mutex> scoped_lock(lock_);
  if (!direct_patching_supported_by_service_) {
    return false;
  }

  return direct_patching_enabled_ = true;
}

void SharedMemoryArbiterImpl::SetDirectSMBPatchingSupportedByService() {
  std::lock_guard<std::mutex> scoped_lock(lock_);
  direct_patching_supported_by_service_ = true;
}

// This function is quite subtle. When making changes keep in mind these two
// challenges:
// 1) If the producer stalls and we happen to be on the |task_runner_| IPC
//    thread (or, for in-process cases, on the same thread where
//    TracingServiceImpl lives), the CommitData() call must be synchronous and
//    not posted, to avoid deadlocks.
// 2) When different threads hit this function, we must guarantee that we don't
//    accidentally make commits out of order. See commit 4e4fe8f56ef and
//    crbug.com/919187 for more context.
void SharedMemoryArbiterImpl::FlushPendingCommitDataRequests(
    std::function<void()> callback) {
  std::unique_ptr<CommitDataRequest> req;
  {
    std::unique_lock<std::mutex> scoped_lock(lock_);

    // Flushing is only supported while |fully_bound_|, and there may still be
    // unbound startup trace writers. If so, skip the commit for now - it'll be
    // done when |fully_bound_| is updated.
    if (!fully_bound_) {
      if (callback)
        pending_flush_callbacks_.push_back(callback);
      return;
    }

    // May be called by TraceWriterImpl on any thread.
    base::TaskRunner* task_runner = task_runner_;
    if (!task_runner->RunsTasksOnCurrentThread()) {
      // We shouldn't post a task while holding a lock. |task_runner| remains
      // valid after unlocking, because |task_runner_| is never reset.
      scoped_lock.unlock();

      auto weak_this = weak_ptr_factory_.GetWeakPtr();
      task_runner->PostTask([weak_this, callback] {
        if (weak_this)
          weak_this->FlushPendingCommitDataRequests(std::move(callback));
      });
      return;
    }

    // |commit_data_req_| could have become a nullptr, for example when a forced
    // sync flush happens in GetNewChunk().
    if (commit_data_req_) {
      // Make sure any placeholder buffer IDs from StartupWriters are replaced
      // before sending the request.
      bool all_placeholders_replaced =
          ReplaceCommitPlaceholderBufferIdsLocked();
      // We're |fully_bound_|, thus all writers are bound and all placeholders
      // should have been replaced.
      PERFETTO_DCHECK(all_placeholders_replaced);

      // In order to allow patching in the producer we delay the kChunkComplete
      // transition and keep batched chunks in the kChunkBeingWritten state.
      // Since we are about to notify the service of all batched chunks, it will
      // not be possible to apply any more patches to them and we need to move
      // them to kChunkComplete - otherwise the service won't look at them.
      for (auto& ctm : *commit_data_req_->mutable_chunks_to_move()) {
        uint32_t header_bitmap = shmem_abi_.GetPageHeaderBitmap(ctm.page());
        auto chunk_state = shmem_abi_.GetChunkStateFromHeaderBitmap(
            header_bitmap, ctm.chunk());
        // Note: the subset of |commit_data_req_| chunks that still need
        // patching is also the subset of chunks that are still being written
        // to. The rest of the chunks in |commit_data_req_| do not need patching
        // and have already been marked as complete.
        if (chunk_state == SharedMemoryABI::kChunkBeingWritten) {
          auto chunk = shmem_abi_.GetChunkUnchecked(ctm.page(), header_bitmap,
                                                    ctm.chunk());
          shmem_abi_.ReleaseChunkAsComplete(std::move(chunk));
        }

        if (use_shmem_emulation_) {
          // When running in the emulation mode:
          // 1. serialize the chunk data to |ctm| as we won't modify the chunk
          // anymore.
          // 2. free the chunk as the service won't be able to do this.
          auto chunk = shmem_abi_.GetChunkUnchecked(ctm.page(), header_bitmap,
                                                    ctm.chunk());
          PERFETTO_CHECK(chunk.is_valid());
          ctm.set_data(chunk.begin(), chunk.size());
          shmem_abi_.ReleaseChunkAsFree(std::move(chunk));
        }
      }

      req = std::move(commit_data_req_);
      bytes_pending_commit_ = 0;
    }
  }  // scoped_lock

  if (req) {
    producer_endpoint_->CommitData(*req, callback);
  } else if (callback) {
    // If |req| was nullptr, it means that an enqueued deferred commit was
    // executed just before this. At this point send an empty commit request
    // to the service, just to linearize with it and give the guarantee to the
    // caller that the data has been flushed into the service.
    producer_endpoint_->CommitData(CommitDataRequest(), std::move(callback));
  }
}

bool SharedMemoryArbiterImpl::TryShutdown() {
  std::lock_guard<std::mutex> scoped_lock(lock_);
  did_shutdown_ = true;
  // Shutdown is safe if there are no active trace writers for this arbiter.
  return active_writer_ids_.IsEmpty();
}

std::unique_ptr<TraceWriter> SharedMemoryArbiterImpl::CreateTraceWriter(
    BufferID target_buffer,
    BufferExhaustedPolicy buffer_exhausted_policy) {
  PERFETTO_CHECK(target_buffer > 0);
  return CreateTraceWriterInternal(target_buffer, buffer_exhausted_policy);
}

std::unique_ptr<TraceWriter> SharedMemoryArbiterImpl::CreateStartupTraceWriter(
    uint16_t target_buffer_reservation_id) {
  return CreateTraceWriterInternal(
      MakeTargetBufferIdForReservation(target_buffer_reservation_id),
      BufferExhaustedPolicy::kDrop);
}

void SharedMemoryArbiterImpl::BindToProducerEndpoint(
    TracingService::ProducerEndpoint* producer_endpoint,
    base::TaskRunner* task_runner) {
  PERFETTO_DCHECK(producer_endpoint && task_runner);
  PERFETTO_DCHECK(task_runner->RunsTasksOnCurrentThread());

  bool should_flush = false;
  std::function<void()> flush_callback;
  {
    std::lock_guard<std::mutex> scoped_lock(lock_);
    PERFETTO_CHECK(!fully_bound_);
    PERFETTO_CHECK(!producer_endpoint_ && !task_runner_);

    producer_endpoint_ = producer_endpoint;
    task_runner_ = task_runner;

    // Now that we're bound to a task runner, also reset the WeakPtrFactory to
    // it. Because this code runs on the task runner, the factory's weak
    // pointers will be valid on it.
    weak_ptr_factory_.Reset(this);

    // All writers registered so far should be startup trace writers, since
    // the producer cannot feasibly know the target buffer for any future
    // session yet.
    for (const auto& entry : pending_writers_) {
      PERFETTO_CHECK(IsReservationTargetBufferId(entry.second));
    }

    // If all buffer reservations are bound, we can flush pending commits.
    if (UpdateFullyBoundLocked()) {
      should_flush = true;
      flush_callback = TakePendingFlushCallbacksLocked();
    }
  }  // scoped_lock

  // Attempt to flush any pending commits (and run pending flush callbacks). If
  // there are none, this will have no effect. If we ended up in a race that
  // changed |fully_bound_| back to false, the commit will happen once we become
  // |fully_bound_| again.
  if (should_flush)
    FlushPendingCommitDataRequests(flush_callback);
}

void SharedMemoryArbiterImpl::BindStartupTargetBuffer(
    uint16_t target_buffer_reservation_id,
    BufferID target_buffer_id) {
  PERFETTO_DCHECK(target_buffer_id > 0);

  std::unique_lock<std::mutex> scoped_lock(lock_);

  // We should already be bound to an endpoint.
  PERFETTO_CHECK(producer_endpoint_);
  PERFETTO_CHECK(task_runner_);
  PERFETTO_CHECK(task_runner_->RunsTasksOnCurrentThread());

  BindStartupTargetBufferImpl(std::move(scoped_lock),
                              target_buffer_reservation_id, target_buffer_id);
}

void SharedMemoryArbiterImpl::AbortStartupTracingForReservation(
    uint16_t target_buffer_reservation_id) {
  std::unique_lock<std::mutex> scoped_lock(lock_);

  // If we are already bound to an arbiter, we may need to flush after aborting
  // the session, and thus should be running on the arbiter's task runner.
  if (task_runner_ && !task_runner_->RunsTasksOnCurrentThread()) {
    // We shouldn't post tasks while locked.
    auto* task_runner = task_runner_;
    scoped_lock.unlock();

    auto weak_this = weak_ptr_factory_.GetWeakPtr();
    task_runner->PostTask([weak_this, target_buffer_reservation_id]() {
      if (!weak_this)
        return;
      weak_this->AbortStartupTracingForReservation(
          target_buffer_reservation_id);
    });
    return;
  }

  // Bind the target buffer reservation to an invalid buffer (ID 0), so that
  // existing commits, as well as future commits (of currently acquired chunks),
  // will be released as free free by the service but otherwise ignored (i.e.
  // not copied into any valid target buffer).
  BindStartupTargetBufferImpl(std::move(scoped_lock),
                              target_buffer_reservation_id,
                              /*target_buffer_id=*/kInvalidBufferId);
}

void SharedMemoryArbiterImpl::BindStartupTargetBufferImpl(
    std::unique_lock<std::mutex> scoped_lock,
    uint16_t target_buffer_reservation_id,
    BufferID target_buffer_id) {
  // We should already be bound to an endpoint if the target buffer is valid.
  PERFETTO_DCHECK((producer_endpoint_ && task_runner_) ||
                  target_buffer_id == kInvalidBufferId);

  PERFETTO_DLOG("Binding startup target buffer reservation %" PRIu16
                " to buffer %" PRIu16,
                target_buffer_reservation_id, target_buffer_id);

  MaybeUnboundBufferID reserved_id =
      MakeTargetBufferIdForReservation(target_buffer_reservation_id);

  bool should_flush = false;
  std::function<void()> flush_callback;
  std::vector<std::pair<WriterID, BufferID>> writers_to_register;

  TargetBufferReservation& reservation =
      target_buffer_reservations_[reserved_id];
  PERFETTO_CHECK(!reservation.resolved);
  reservation.resolved = true;
  reservation.target_buffer = target_buffer_id;

  // Collect trace writers associated with the reservation.
  for (auto it = pending_writers_.begin(); it != pending_writers_.end();) {
    if (it->second == reserved_id) {
      // No need to register writers that have an invalid target buffer.
      if (target_buffer_id != kInvalidBufferId) {
        writers_to_register.push_back(
            std::make_pair(it->first, target_buffer_id));
      }
      it = pending_writers_.erase(it);
    } else {
      it++;
    }
  }

  // If all buffer reservations are bound, we can flush pending commits.
  if (UpdateFullyBoundLocked()) {
    should_flush = true;
    flush_callback = TakePendingFlushCallbacksLocked();
  }

  scoped_lock.unlock();

  // Register any newly bound trace writers with the service.
  for (const auto& writer_and_target_buffer : writers_to_register) {
    producer_endpoint_->RegisterTraceWriter(writer_and_target_buffer.first,
                                            writer_and_target_buffer.second);
  }

  // Attempt to flush any pending commits (and run pending flush callbacks). If
  // there are none, this will have no effect. If we ended up in a race that
  // changed |fully_bound_| back to false, the commit will happen once we become
  // |fully_bound_| again.
  if (should_flush)
    FlushPendingCommitDataRequests(flush_callback);
}

SharedMemoryArbiterImpl::Stats SharedMemoryArbiterImpl::GetStats() {
  std::lock_guard<std::mutex> scoped_lock(lock_);
  Stats res;

  for (size_t page_idx = 0; page_idx < shmem_abi_.num_pages(); page_idx++) {
    uint32_t bitmap = shmem_abi_.page_header(page_idx)->header_bitmap.load(
        std::memory_order_relaxed);
    SharedMemoryABI::PageLayout layout =
        SharedMemoryABI::GetLayoutFromHeaderBitmap(bitmap);
    if (layout == SharedMemoryABI::kPageNotPartitioned) {
      res.pages_free++;
    } else if (layout == SharedMemoryABI::kPageDivReserved1 ||
               layout == SharedMemoryABI::kPageDivReserved2) {
      res.pages_unexpected++;
    }
    // Free and unexpected pages have zero chunks.
    const uint32_t num_chunks =
        SharedMemoryABI::GetNumChunksFromHeaderBitmap(bitmap);
    for (uint32_t i = 0; i < num_chunks; i++) {
      switch (SharedMemoryABI::GetChunkStateFromHeaderBitmap(bitmap, i)) {
        case SharedMemoryABI::kChunkFree:
          res.chunks_free++;
          break;
        case SharedMemoryABI::kChunkBeingWritten:
          res.chunks_being_written++;
          break;
        case SharedMemoryABI::kChunkBeingRead:
          res.chunks_being_read++;
          break;
        case SharedMemoryABI::kChunkComplete:
          res.chunks_complete++;
          break;
      }
    }
  }

  return res;
}

std::function<void()>
SharedMemoryArbiterImpl::TakePendingFlushCallbacksLocked() {
  if (pending_flush_callbacks_.empty())
    return std::function<void()>();

  std::vector<std::function<void()>> pending_flush_callbacks;
  pending_flush_callbacks.swap(pending_flush_callbacks_);
  // Capture the callback list into the lambda by copy.
  return [pending_flush_callbacks]() {
    for (auto& callback : pending_flush_callbacks)
      callback();
  };
}

void SharedMemoryArbiterImpl::NotifyFlushComplete(FlushRequestID req_id) {
  base::TaskRunner* task_runner_to_commit_on = nullptr;

  {
    std::lock_guard<std::mutex> scoped_lock(lock_);
    // If a commit_data_req_ exists it means that somebody else already posted a
    // FlushPendingCommitDataRequests() task.
    if (!commit_data_req_) {
      commit_data_req_.reset(new CommitDataRequest());

      // Flushing the commit is only supported while we're |fully_bound_|. If we
      // aren't, we'll flush when |fully_bound_| is updated.
      if (fully_bound_)
        task_runner_to_commit_on = task_runner_;
    } else {
      // If there is another request queued and that also contains is a reply
      // to a flush request, reply with the highest id.
      req_id = std::max(req_id, commit_data_req_->flush_request_id());
    }
    commit_data_req_->set_flush_request_id(req_id);
  }  // scoped_lock

  // We shouldn't post tasks while locked. |task_runner_to_commit_on|
  // remains valid after unlocking, because |task_runner_| is never reset.
  if (task_runner_to_commit_on) {
    auto weak_this = weak_ptr_factory_.GetWeakPtr();
    task_runner_to_commit_on->PostTask([weak_this] {
      if (weak_this)
        weak_this->FlushPendingCommitDataRequests();
    });
  }
}

std::unique_ptr<TraceWriter> SharedMemoryArbiterImpl::CreateTraceWriterInternal(
    MaybeUnboundBufferID target_buffer,
    BufferExhaustedPolicy buffer_exhausted_policy) {
  WriterID id;
  base::TaskRunner* task_runner_to_register_on = nullptr;

  {
    std::lock_guard<std::mutex> scoped_lock(lock_);
    if (did_shutdown_)
      return std::unique_ptr<TraceWriter>(new NullTraceWriter());

    id = active_writer_ids_.Allocate();
    if (!id)
      return std::unique_ptr<TraceWriter>(new NullTraceWriter());

    PERFETTO_DCHECK(!pending_writers_.count(id));

    if (IsReservationTargetBufferId(target_buffer)) {
      // If the reservation is new, mark it as unbound in
      // |target_buffer_reservations_|. Otherwise, if the reservation was
      // already bound, choose the bound buffer ID now.
      auto it_and_inserted = target_buffer_reservations_.insert(
          {target_buffer, TargetBufferReservation()});
      if (it_and_inserted.first->second.resolved)
        target_buffer = it_and_inserted.first->second.target_buffer;
    }

    if (IsReservationTargetBufferId(target_buffer)) {
      // The arbiter and/or startup buffer reservations are not bound yet, so
      // buffer the registration of the writer until after we're bound.
      pending_writers_[id] = target_buffer;

      // Mark the arbiter as not fully bound, since we now have at least one
      // unbound trace writer / target buffer reservation.
      fully_bound_ = false;
      was_always_bound_ = false;
    } else if (target_buffer != kInvalidBufferId) {
      // Trace writer is bound, so arbiter should be bound to an endpoint, too.
      PERFETTO_CHECK(producer_endpoint_ && task_runner_);
      task_runner_to_register_on = task_runner_;
    }

    // All trace writers must use kDrop policy if the arbiter ever becomes
    // unbound.
    bool uses_drop_policy =
        buffer_exhausted_policy == BufferExhaustedPolicy::kDrop;
    all_writers_have_drop_policy_ &= uses_drop_policy;
    PERFETTO_DCHECK(fully_bound_ || uses_drop_policy);
    PERFETTO_CHECK(fully_bound_ || all_writers_have_drop_policy_);
    PERFETTO_CHECK(was_always_bound_ || uses_drop_policy);
  }  // scoped_lock

  // We shouldn't post tasks while locked. |task_runner_to_register_on|
  // remains valid after unlocking, because |task_runner_| is never reset.
  if (task_runner_to_register_on) {
    auto weak_this = weak_ptr_factory_.GetWeakPtr();
    task_runner_to_register_on->PostTask([weak_this, id, target_buffer] {
      if (weak_this)
        weak_this->producer_endpoint_->RegisterTraceWriter(id, target_buffer);
    });
  }

  return std::unique_ptr<TraceWriter>(
      new TraceWriterImpl(this, id, target_buffer, buffer_exhausted_policy));
}

void SharedMemoryArbiterImpl::ReleaseWriterID(WriterID id) {
  base::TaskRunner* task_runner = nullptr;
  base::WeakPtr<SharedMemoryArbiterImpl> weak_this;
  {
    std::lock_guard<std::mutex> scoped_lock(lock_);
    active_writer_ids_.Free(id);

    auto it = pending_writers_.find(id);
    if (it != pending_writers_.end()) {
      // Writer hasn't been bound yet and thus also not yet registered with the
      // service.
      pending_writers_.erase(it);
      return;
    }

    // A trace writer from an aborted session may be destroyed before the
    // arbiter is bound to a task runner. In that case, it was never registered
    // with the service.
    if (!task_runner_)
      return;

    // If `active_writer_ids_` is empty, `TryShutdown()` can return true
    // and `*this` can be deleted. Let's grab everything we need from `*this`
    // before releasing the lock.
    weak_this = weak_ptr_factory_.GetWeakPtr();
    task_runner = task_runner_;
  }  // scoped_lock

  // We shouldn't post tasks while locked. |task_runner| remains valid after
  // unlocking, because |task_runner_| is never reset.
  task_runner->PostTask([weak_this, id] {
    if (weak_this)
      weak_this->producer_endpoint_->UnregisterTraceWriter(id);
  });
}

bool SharedMemoryArbiterImpl::ReplaceCommitPlaceholderBufferIdsLocked() {
  if (!commit_data_req_)
    return true;

  bool all_placeholders_replaced = true;
  for (auto& chunk : *commit_data_req_->mutable_chunks_to_move()) {
    if (!IsReservationTargetBufferId(chunk.target_buffer()))
      continue;
    const auto it = target_buffer_reservations_.find(chunk.target_buffer());
    PERFETTO_DCHECK(it != target_buffer_reservations_.end());
    if (!it->second.resolved) {
      all_placeholders_replaced = false;
      continue;
    }
    chunk.set_target_buffer(it->second.target_buffer);
  }
  for (auto& chunk : *commit_data_req_->mutable_chunks_to_patch()) {
    if (!IsReservationTargetBufferId(chunk.target_buffer()))
      continue;
    const auto it = target_buffer_reservations_.find(chunk.target_buffer());
    PERFETTO_DCHECK(it != target_buffer_reservations_.end());
    if (!it->second.resolved) {
      all_placeholders_replaced = false;
      continue;
    }
    chunk.set_target_buffer(it->second.target_buffer);
  }
  return all_placeholders_replaced;
}

bool SharedMemoryArbiterImpl::UpdateFullyBoundLocked() {
  if (!producer_endpoint_) {
    PERFETTO_DCHECK(!fully_bound_);
    return false;
  }
  // We're fully bound if all target buffer reservations have a valid associated
  // BufferID.
  fully_bound_ = std::none_of(
      target_buffer_reservations_.begin(), target_buffer_reservations_.end(),
      [](std::pair<MaybeUnboundBufferID, TargetBufferReservation> entry) {
        return !entry.second.resolved;
      });
  if (!fully_bound_)
    was_always_bound_ = false;
  return fully_bound_;
}

}  // namespace perfetto
