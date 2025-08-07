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

#include "src/tracing/core/trace_writer_impl.h"

#include <string.h>

#include <algorithm>
#include <type_traits>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/thread_annotations.h"
#include "perfetto/protozero/message.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/protozero/root_message.h"
#include "perfetto/protozero/static_buffer.h"
#include "perfetto/tracing/buffer_exhausted_policy.h"
#include "src/tracing/core/shared_memory_arbiter_impl.h"

#include "protos/perfetto/trace/trace_packet.pbzero.h"

using protozero::proto_utils::kMessageLengthFieldSize;
using protozero::proto_utils::WriteRedundantVarInt;
using ChunkHeader = perfetto::SharedMemoryABI::ChunkHeader;

namespace perfetto {

namespace {
constexpr size_t kPacketHeaderSize = SharedMemoryABI::kPacketHeaderSize;
// The -1 is because we want to leave extra room to inflate the counter.
constexpr size_t kMaxPacketsPerChunk = ChunkHeader::Packets::kMaxCount - 1;
// When the packet count in a chunk is inflated, TraceWriter is always going to
// leave this kExtraRoomForInflatedPacket bytes to write an empty trace packet
// if it needs to.
constexpr size_t kExtraRoomForInflatedPacket = 1;
uint8_t g_garbage_chunk[1024];
}  // namespace

TraceWriterImpl::TraceWriterImpl(SharedMemoryArbiterImpl* shmem_arbiter,
                                 WriterID id,
                                 MaybeUnboundBufferID target_buffer,
                                 BufferExhaustedPolicy buffer_exhausted_policy)
    : shmem_arbiter_(shmem_arbiter),
      id_(id),
      target_buffer_(target_buffer),
      buffer_exhausted_policy_(buffer_exhausted_policy),
      protobuf_stream_writer_(this),
      process_id_(base::GetProcessId()) {
  // TODO(primiano): we could handle the case of running out of TraceWriterID(s)
  // more gracefully and always return a no-op TracePacket in NewTracePacket().
  PERFETTO_CHECK(id_ != 0);

  cur_packet_.reset(new protozero::RootMessage<protos::pbzero::TracePacket>());
  cur_packet_->Finalize();  // To avoid the CHECK in NewTracePacket().
}

TraceWriterImpl::~TraceWriterImpl() {
  if (cur_chunk_.is_valid()) {
    cur_packet_->Finalize();
    Flush();
  }
  // This call may cause the shared memory arbiter (and the underlying memory)
  // to get asynchronously deleted if this was the last trace writer targeting
  // the arbiter and the arbiter was marked for shutdown.
  shmem_arbiter_->ReleaseWriterID(id_);
}

void TraceWriterImpl::ReturnCompletedChunk() {
  PERFETTO_DCHECK(cur_chunk_.is_valid());
  if (cur_chunk_packet_count_inflated_) {
    uint8_t zero_size = 0;
    static_assert(sizeof zero_size == kExtraRoomForInflatedPacket);
    PERFETTO_CHECK(protobuf_stream_writer_.bytes_available() != 0);
    protobuf_stream_writer_.WriteBytesUnsafe(&zero_size, sizeof zero_size);
    cur_chunk_packet_count_inflated_ = false;
  }
  shmem_arbiter_->ReturnCompletedChunk(std::move(cur_chunk_), target_buffer_,
                                       &patch_list_);
}

void TraceWriterImpl::Flush(std::function<void()> callback) {
  // Flush() cannot be called in the middle of a TracePacket.
  PERFETTO_CHECK(cur_packet_->is_finalized());
  // cur_packet_ is finalized: that means that the size is correct for all the
  // nested submessages. The root fragment size however is not handled by
  // protozero::Message::Finalize() and must be filled here.
  FinalizeFragmentIfRequired();

  if (cur_chunk_.is_valid()) {
    ReturnCompletedChunk();
  } else {
    // When in stall mode, all patches should have been returned with the last
    // chunk, since the last packet was completed. In drop_packets_ mode, this
    // may not be the case because the packet may have been fragmenting when
    // SMB exhaustion occurred and |cur_chunk_| became invalid. In this case,
    // drop_packets_ should be true.
    PERFETTO_DCHECK(patch_list_.empty() || drop_packets_);
  }

  // Always issue the Flush request, even if there is nothing to flush, just
  // for the sake of getting the callback posted back.
  shmem_arbiter_->FlushPendingCommitDataRequests(callback);
  protobuf_stream_writer_.Reset({nullptr, nullptr});
}

TraceWriterImpl::TracePacketHandle TraceWriterImpl::NewTracePacket() {
  // If we hit this, the caller is calling NewTracePacket() without having
  // finalized the previous packet.
  PERFETTO_CHECK(cur_packet_->is_finalized());
  // If we hit this, this trace writer was created in a different process. This
  // likely means that the process forked while tracing was active, and the
  // forked child process tried to emit a trace event. This is not supported, as
  // it would lead to two processes writing to the same tracing SMB.
  PERFETTO_DCHECK(process_id_ == base::GetProcessId());

  // Before starting a new packet, make sure that the last fragment size has ben
  // written correctly. The root fragment size is not written by
  // protozero::Message::Finalize().
  FinalizeFragmentIfRequired();

  fragmenting_packet_ = false;

  // Reserve space for the size of the message. Note: this call might re-enter
  // into this class invoking GetNewBuffer() if there isn't enough space or if
  // this is the very first call to NewTracePacket().
  static_assert(kPacketHeaderSize == kMessageLengthFieldSize,
                "The packet header must match the Message header size");

  bool was_dropping_packets = drop_packets_;

  // It doesn't make sense to begin a packet that is going to fragment
  // immediately after (8 is just an arbitrary estimation on the minimum size of
  // a realistic packet).
  bool chunk_too_full =
      protobuf_stream_writer_.bytes_available() < kPacketHeaderSize + 8;
  if (chunk_too_full || reached_max_packets_per_chunk_ ||
      retry_new_chunk_after_packet_) {
    protobuf_stream_writer_.Reset(GetNewBuffer());
  }

  // Send any completed patches to the service to facilitate trace data
  // recovery by the service. This should only happen when we're completing
  // the first packet in a chunk which was a continuation from the previous
  // chunk, i.e. at most once per chunk.
  if (!patch_list_.empty() && patch_list_.front().is_patched()) {
    shmem_arbiter_->SendPatches(id_, target_buffer_, &patch_list_);
  }

  cur_packet_->Reset(&protobuf_stream_writer_);
  uint8_t* header = protobuf_stream_writer_.ReserveBytes(kPacketHeaderSize);
  memset(header, 0, kPacketHeaderSize);
  cur_fragment_size_field_ = header;

  TracePacketHandle handle(cur_packet_.get());
  cur_fragment_start_ = protobuf_stream_writer_.write_ptr();
  fragmenting_packet_ = true;

  if (PERFETTO_LIKELY(!drop_packets_)) {
    uint16_t new_packet_count;
    if (cur_chunk_packet_count_inflated_) {
      new_packet_count =
          cur_chunk_.header()->packets.load(std::memory_order_relaxed).count;
      cur_chunk_packet_count_inflated_ = false;
    } else {
      new_packet_count = cur_chunk_.IncrementPacketCount();
    }
    reached_max_packets_per_chunk_ = new_packet_count == kMaxPacketsPerChunk;

    if (PERFETTO_UNLIKELY(was_dropping_packets)) {
      // We've succeeded to get a new chunk from the SMB after we entered
      // drop_packets_ mode. Record a marker into the new packet to indicate the
      // data loss.
      cur_packet_->set_previous_packet_dropped(true);
    }
  }

  if (PERFETTO_UNLIKELY(first_packet_on_sequence_)) {
    cur_packet_->set_first_packet_on_sequence(true);
    first_packet_on_sequence_ = false;
  }

  handle.set_finalization_listener(this);

  return handle;
}

// Called by the Message. We can get here in two cases:
// 1. In the middle of writing a Message,
// when |fragmenting_packet_| == true. In this case we want to update the
// chunk header with a partial packet and start a new partial packet in the
// new chunk.
// 2. While calling ReserveBytes() for the packet header in NewTracePacket().
// In this case |fragmenting_packet_| == false and we just want a new chunk
// without creating any fragments.
protozero::ContiguousMemoryRange TraceWriterImpl::GetNewBuffer() {
  if (fragmenting_packet_ && drop_packets_) {
    // We can't write the remaining data of the fragmenting packet to a new
    // chunk, because we have already lost some of its data in the garbage
    // chunk. Thus, we will wrap around in the garbage chunk, wait until the
    // current packet was completed, and then attempt to get a new chunk from
    // the SMB again. Instead, if |drop_packets_| is true and
    // |fragmenting_packet_| is false, we try to acquire a valid chunk because
    // the SMB exhaustion might be resolved.
    retry_new_chunk_after_packet_ = true;
    cur_fragment_size_field_ = nullptr;
    cur_fragment_start_ = &g_garbage_chunk[0];
    return protozero::ContiguousMemoryRange{
        &g_garbage_chunk[0], &g_garbage_chunk[0] + sizeof(g_garbage_chunk)};
  }

  // Attempt to grab the next chunk before finalizing the current one, so that
  // we know whether we need to start dropping packets before writing the
  // current packet fragment's header.
  ChunkHeader::Packets packets = {};
  if (fragmenting_packet_) {
    packets.count = 1;
    packets.flags = ChunkHeader::kFirstPacketContinuesFromPrevChunk;
  }

  // The memory order of the stores below doesn't really matter. This |header|
  // is just a local temporary object. The GetNewChunk() call below will copy it
  // into the shared buffer with the proper barriers.
  ChunkHeader header = {};
  header.writer_id.store(id_, std::memory_order_relaxed);
  header.chunk_id.store(next_chunk_id_, std::memory_order_relaxed);
  header.packets.store(packets, std::memory_order_relaxed);

  BufferExhaustedPolicy policy = buffer_exhausted_policy_;
  if (policy == BufferExhaustedPolicy::kStallThenDrop && drop_packets_) {
    policy = BufferExhaustedPolicy::kDrop;
  }

  SharedMemoryABI::Chunk new_chunk =
      shmem_arbiter_->GetNewChunk(header, policy);
  if (!new_chunk.is_valid()) {
    // Shared memory buffer exhausted, switch into |drop_packets_| mode. We'll
    // drop data until the garbage chunk has been filled once and then retry.

    // If we started a packet in one of the previous (valid) chunks, we need to
    // tell the service to discard it.
    if (fragmenting_packet_) {
      // We can only end up here if the previous chunk was a valid chunk,
      // because we never try to acquire a new chunk in |drop_packets_| mode
      // while fragmenting.
      PERFETTO_DCHECK(!drop_packets_);

      // Backfill the last fragment's header with an invalid size (too large),
      // so that the service's TraceBuffer throws out the incomplete packet.
      // It'll restart reading from the next chunk we submit.
      WriteRedundantVarInt(SharedMemoryABI::kPacketSizeDropPacket,
                           cur_fragment_size_field_);

      // Reset the size field, since we should not write the current packet's
      // size anymore after this.
      cur_fragment_size_field_ = nullptr;

      // We don't set kLastPacketContinuesOnNextChunk or kChunkNeedsPatching on
      // the last chunk, because its last fragment will be discarded anyway.
      // However, the current packet fragment points to a valid |cur_chunk_| and
      // may have non-finalized nested messages which will continue in the
      // garbage chunk and currently still point into |cur_chunk_|. As we are
      // about to return |cur_chunk_|, we need to invalidate the size fields of
      // those nested messages. Normally we move them in the |patch_list_| (see
      // below) but in this case, it doesn't make sense to send patches for a
      // fragment that will be discarded for sure. Thus, we clean up any size
      // field references into |cur_chunk_|.
      for (auto* nested_msg = cur_packet_->nested_message(); nested_msg;
           nested_msg = nested_msg->nested_message()) {
        uint8_t* const cur_hdr = nested_msg->size_field();

        // If this is false the protozero Message has already been instructed to
        // write, upon Finalize(), its size into the patch list.
        bool size_field_points_within_chunk =
            cur_hdr >= cur_chunk_.payload_begin() &&
            cur_hdr + kMessageLengthFieldSize <= cur_chunk_.end();

        if (size_field_points_within_chunk)
          nested_msg->set_size_field(nullptr);
      }
    } else if (!drop_packets_ && cur_fragment_size_field_) {
      // If we weren't dropping packets before, we should indicate to the
      // service that we're about to lose data. We do this by invalidating the
      // size of the last packet in |cur_chunk_|. The service will record
      // statistics about packets with kPacketSizeDropPacket size.
      PERFETTO_DCHECK(cur_packet_->is_finalized());
      PERFETTO_DCHECK(cur_chunk_.is_valid());

      // |cur_fragment_size_field_| should point within |cur_chunk_|'s payload.
      PERFETTO_DCHECK(cur_fragment_size_field_ >= cur_chunk_.payload_begin() &&
                      cur_fragment_size_field_ + kMessageLengthFieldSize <=
                          cur_chunk_.end());

      WriteRedundantVarInt(SharedMemoryABI::kPacketSizeDropPacket,
                           cur_fragment_size_field_);
    }

    if (cur_chunk_.is_valid()) {
      ReturnCompletedChunk();
    }

    // Only increment the count if we are newly entering this state not
    // otherwise.
    drop_count_ += !drop_packets_;
    drop_packets_ = true;
    cur_chunk_ = SharedMemoryABI::Chunk();  // Reset to an invalid chunk.
    cur_chunk_packet_count_inflated_ = false;
    reached_max_packets_per_chunk_ = false;
    retry_new_chunk_after_packet_ = false;
    cur_fragment_size_field_ = nullptr;
    cur_fragment_start_ = &g_garbage_chunk[0];

    PERFETTO_ANNOTATE_BENIGN_RACE_SIZED(&g_garbage_chunk,
                                        sizeof(g_garbage_chunk),
                                        "nobody reads the garbage chunk")
    return protozero::ContiguousMemoryRange{
        &g_garbage_chunk[0], &g_garbage_chunk[0] + sizeof(g_garbage_chunk)};
  }  // if (!new_chunk.is_valid())

  PERFETTO_DCHECK(new_chunk.is_valid());

  if (fragmenting_packet_) {
    // We should not be fragmenting a packet after we exited drop_packets_ mode,
    // because we only retry to get a new chunk when a fresh packet is started.
    PERFETTO_DCHECK(!drop_packets_);

    uint8_t* const wptr = protobuf_stream_writer_.write_ptr();
    PERFETTO_DCHECK(wptr >= cur_fragment_start_);
    uint32_t partial_size = static_cast<uint32_t>(wptr - cur_fragment_start_);
    PERFETTO_DCHECK(partial_size < cur_chunk_.size());

    // Backfill the packet header with the fragment size.
    PERFETTO_DCHECK(partial_size > 0);
    cur_chunk_.SetFlag(ChunkHeader::kLastPacketContinuesOnNextChunk);
    WriteRedundantVarInt(partial_size, cur_fragment_size_field_);

    // Descend in the stack of non-finalized nested submessages (if any) and
    // detour their |size_field| into the |patch_list_|. At this point we have
    // to release the chunk and they cannot write anymore into that.
    for (auto* nested_msg = cur_packet_->nested_message(); nested_msg;
         nested_msg = nested_msg->nested_message()) {
      uint8_t* cur_hdr = nested_msg->size_field();

      // If this is false the protozero Message has already been instructed to
      // write, upon Finalize(), its size into the patch list.
      bool size_field_points_within_chunk =
          cur_hdr >= cur_chunk_.payload_begin() &&
          cur_hdr + kMessageLengthFieldSize <= cur_chunk_.end();

      if (size_field_points_within_chunk) {
        cur_hdr = TraceWriterImpl::AnnotatePatch(cur_hdr);
        nested_msg->set_size_field(cur_hdr);
      } else {
#if PERFETTO_DCHECK_IS_ON()
        // Ensure that the size field of the message points to an element of the
        // patch list.
        auto patch_it = std::find_if(
            patch_list_.begin(), patch_list_.end(),
            [cur_hdr](const Patch& p) { return &p.size_field[0] == cur_hdr; });
        PERFETTO_DCHECK(patch_it != patch_list_.end());
#endif
      }
    }  // for(nested_msg)
  }  // if(fragmenting_packet)

  if (cur_chunk_.is_valid()) {
    // ReturnCompletedChunk will consume the first patched entries from
    // |patch_list_| and shrink it.
    ReturnCompletedChunk();
  }

  // Switch to the new chunk.
  drop_packets_ = false;
  reached_max_packets_per_chunk_ = false;
  retry_new_chunk_after_packet_ = false;
  next_chunk_id_++;
  cur_chunk_ = std::move(new_chunk);
  cur_chunk_packet_count_inflated_ = false;
  cur_fragment_size_field_ = nullptr;

  uint8_t* payload_begin = cur_chunk_.payload_begin();
  if (fragmenting_packet_) {
    cur_fragment_size_field_ = payload_begin;
    memset(payload_begin, 0, kPacketHeaderSize);
    payload_begin += kPacketHeaderSize;
    cur_fragment_start_ = payload_begin;
  }

  return protozero::ContiguousMemoryRange{payload_begin, cur_chunk_.end()};
}

void TraceWriterImpl::FinishTracePacket() {
  // If we hit this, this trace writer was created in a different process. This
  // likely means that the process forked while tracing was active, and the
  // forked child process tried to emit a trace event. This is not supported, as
  // it would lead to two processes writing to the same tracing SMB.
  PERFETTO_DCHECK(process_id_ == base::GetProcessId());

  FinalizeFragmentIfRequired();

  cur_packet_->Reset(&protobuf_stream_writer_);
  cur_packet_->Finalize();  // To avoid the CHECK in NewTracePacket().

  // cur_chunk_packet_count_inflated_ can be true if FinishTracePacket() is
  // called multiple times.
  if (cur_chunk_.is_valid() && !cur_chunk_packet_count_inflated_) {
    if (protobuf_stream_writer_.bytes_available() <
        kExtraRoomForInflatedPacket) {
      ReturnCompletedChunk();
    } else {
      cur_chunk_packet_count_inflated_ = true;
      cur_chunk_.IncrementPacketCount();
    }
  }

  // Send any completed patches to the service to facilitate trace data
  // recovery by the service. This should only happen when we're completing
  // the first packet in a chunk which was a continuation from the previous
  // chunk, i.e. at most once per chunk.
  if (!patch_list_.empty() && patch_list_.front().is_patched()) {
    shmem_arbiter_->SendPatches(id_, target_buffer_, &patch_list_);
  }
}

void TraceWriterImpl::FinalizeFragmentIfRequired() {
  if (!cur_fragment_size_field_) {
    return;
  }
  uint8_t* const wptr = protobuf_stream_writer_.write_ptr();
  PERFETTO_DCHECK(wptr >= cur_fragment_start_);
  uint32_t partial_size = static_cast<uint32_t>(wptr - cur_fragment_start_);

  // cur_fragment_size_field_, if not nullptr, is always inside or immediately
  // before protobuf_stream_writer_.cur_range().
  if (partial_size < protozero::proto_utils::kMaxOneByteMessageLength &&
      cur_fragment_size_field_ >= protobuf_stream_writer_.cur_range().begin) {
    // This handles compaction of the root message. For nested messages, the
    // compaction is handled by protozero::Message::Finalize().
    protobuf_stream_writer_.Rewind(
        partial_size, protozero::proto_utils::kMessageLengthFieldSize - 1u);
    *cur_fragment_size_field_ = static_cast<uint8_t>(partial_size);
  } else {
    WriteRedundantVarInt(partial_size, cur_fragment_size_field_);
  }
  cur_fragment_size_field_ = nullptr;
}

uint8_t* TraceWriterImpl::AnnotatePatch(uint8_t* to_patch) {
  if (!cur_chunk_.is_valid()) {
    return nullptr;
  }
  auto offset = static_cast<uint16_t>(to_patch - cur_chunk_.payload_begin());
  const ChunkID cur_chunk_id =
      cur_chunk_.header()->chunk_id.load(std::memory_order_relaxed);
  static_assert(kPatchSize == sizeof(Patch::PatchContent),
                "Patch size mismatch");
  Patch* patch = patch_list_.emplace_back(cur_chunk_id, offset);
  // Check that the flag is not already set before setting it. This is not
  // necessary, but it makes the code faster.
  if (!(cur_chunk_.GetPacketCountAndFlags().second &
        ChunkHeader::kChunkNeedsPatching)) {
    cur_chunk_.SetFlag(ChunkHeader::kChunkNeedsPatching);
  }
  return &patch->size_field[0];
}

void TraceWriterImpl::OnMessageFinalized(protozero::Message*) {
  TraceWriterImpl::FinishTracePacket();
}

WriterID TraceWriterImpl::writer_id() const {
  return id_;
}

// Base class definitions.
TraceWriter::TraceWriter() = default;
TraceWriter::~TraceWriter() = default;

}  // namespace perfetto
