/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/tracing/service/trace_buffer_v1.h"

#include <limits>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flags.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/tracing/core/client_identity.h"
#include "perfetto/ext/tracing/core/shared_memory_abi.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/protozero/proto_utils.h"

#define TRACE_BUFFER_VERBOSE_LOGGING() 0  // Set to 1 when debugging unittests.
#if TRACE_BUFFER_VERBOSE_LOGGING()
#define TRACE_BUFFER_DLOG PERFETTO_DLOG
#else
#define TRACE_BUFFER_DLOG(...) void()
#endif

namespace perfetto {

namespace {
constexpr uint8_t kFirstPacketContinuesFromPrevChunk =
    SharedMemoryABI::ChunkHeader::kFirstPacketContinuesFromPrevChunk;
constexpr uint8_t kLastPacketContinuesOnNextChunk =
    SharedMemoryABI::ChunkHeader::kLastPacketContinuesOnNextChunk;
constexpr uint8_t kChunkNeedsPatching =
    SharedMemoryABI::ChunkHeader::kChunkNeedsPatching;
}  // namespace.

// static
std::unique_ptr<TraceBufferV1> TraceBufferV1::Create(size_t size_in_bytes,
                                                     OverwritePolicy pol) {
  std::unique_ptr<TraceBufferV1> trace_buffer(new TraceBufferV1(pol));
  if (!trace_buffer->Initialize(size_in_bytes))
    return nullptr;
  return trace_buffer;
}

TraceBufferV1::TraceBufferV1(OverwritePolicy pol) : overwrite_policy_(pol) {
  // See comments in ChunkRecord for the rationale of this.
  static_assert(sizeof(ChunkRecord) == sizeof(SharedMemoryABI::PageHeader) +
                                           sizeof(SharedMemoryABI::ChunkHeader),
                "ChunkRecord out of sync with the layout of SharedMemoryABI");
  static_assert(sizeof(ChunkRecord) == TraceBuffer::InlineChunkHeaderSize);
}

TraceBufferV1::~TraceBufferV1() = default;

bool TraceBufferV1::Initialize(size_t size) {
  static_assert(
      SharedMemoryABI::kMinPageSize % sizeof(ChunkRecord) == 0,
      "sizeof(ChunkRecord) must be an integer divider of a page size");
  auto max_size = std::numeric_limits<decltype(ChunkMeta::record_off)>::max();
  PERFETTO_CHECK(size <= static_cast<size_t>(max_size));
  data_ = base::PagedMemory::Allocate(
      size, base::PagedMemory::kMayFail | base::PagedMemory::kDontCommit);
  if (!data_.IsValid()) {
    PERFETTO_ELOG("Trace buffer allocation failed (size: %zu)", size);
    return false;
  }
  size_ = size;
  used_size_ = 0;
  stats_.set_buffer_size(size);
  max_chunk_size_ = std::min(size, ChunkRecord::kMaxSize);
  wptr_ = begin();
  index_.clear();
  last_chunk_id_written_.clear();
  read_iter_ = GetReadIterForSequence(index_.end());
  return true;
}

// Note: |src| points to a shmem region that is shared with the producer. Assume
// that the producer is malicious and will change the content of |src|
// while we execute here. Don't do any processing on it other than memcpy().
void TraceBufferV1::CopyChunkUntrusted(
    ProducerID producer_id_trusted,
    const ClientIdentity& client_identity_trusted,
    WriterID writer_id,
    ChunkID chunk_id,
    uint16_t num_fragments,
    uint8_t chunk_flags,
    bool chunk_complete,
    const uint8_t* src,
    size_t size) {
  PERFETTO_CHECK(!read_only_);

  // |record_size| = |size| + sizeof(ChunkRecord), rounded up to avoid to end
  // up in a fragmented state where size_to_end() < sizeof(ChunkRecord).
  const size_t record_size =
      base::AlignUp<sizeof(ChunkRecord)>(size + sizeof(ChunkRecord));
  TRACE_BUFFER_DLOG("CopyChunk @ %" PRIdPTR ", size=%zu", wptr_ - begin(),
                    record_size);
  if (PERFETTO_UNLIKELY(record_size > max_chunk_size_)) {
    stats_.set_abi_violations(stats_.abi_violations() + 1);
    PERFETTO_DCHECK(suppress_client_dchecks_for_testing_);
    return;
  }

  has_data_ = true;
#if PERFETTO_DCHECK_IS_ON()
  changed_since_last_read_ = true;
#endif

  // If the chunk hasn't been completed, we should only consider the first
  // |num_fragments - 1| packets complete. For simplicity, we simply disregard
  // the last one when we copy the chunk.
  if (PERFETTO_UNLIKELY(!chunk_complete)) {
    if (num_fragments > 0) {
      num_fragments--;
      // These flags should only affect the last packet in the chunk. We clear
      // them, so that TraceBufferV1 is able to look at the remaining packets in
      // this chunk.
      chunk_flags &= ~kLastPacketContinuesOnNextChunk;
      chunk_flags &= ~kChunkNeedsPatching;
    }
  }

  ChunkRecord record(record_size);
  record.producer_id = producer_id_trusted;
  record.chunk_id = chunk_id;
  record.writer_id = writer_id;
  record.num_fragments = num_fragments;
  record.flags = chunk_flags & ChunkRecord::kFlagsBitMask;
  ChunkMeta::Key key(record);

  // Check whether we have already copied the same chunk previously. This may
  // happen if the service scrapes chunks in a potentially incomplete state
  // before receiving commit requests for them from the producer. Note that the
  // service may scrape and thus override chunks in arbitrary order since the
  // chunks aren't ordered in the SMB.
  const auto it = index_.find(key);
  if (PERFETTO_UNLIKELY(it != index_.end())) {
    ChunkMeta* record_meta = &it->second;
    ChunkRecord* prev = GetChunkRecordAt(begin() + record_meta->record_off);

    // Verify that the old chunk's metadata corresponds to the new one.
    // Overridden chunks should never change size, since the page layout is
    // fixed per writer. The number of fragments should also never decrease and
    // flags should not be removed.
    if (PERFETTO_UNLIKELY(ChunkMeta::Key(*prev) != key ||
                          prev->size != record_size ||
                          prev->num_fragments > num_fragments ||
                          (prev->flags & chunk_flags) != prev->flags)) {
      stats_.set_abi_violations(stats_.abi_violations() + 1);
      PERFETTO_DCHECK(suppress_client_dchecks_for_testing_);
      return;
    }

    // If this chunk was previously copied with the same number of fragments and
    // the number didn't change, there's no need to copy it again. If the
    // previous chunk was complete already, this should always be the case.
    PERFETTO_DCHECK(suppress_client_dchecks_for_testing_ ||
                    !record_meta->is_complete() ||
                    (chunk_complete && prev->num_fragments == num_fragments));
    if (prev->num_fragments == num_fragments) {
      TRACE_BUFFER_DLOG("  skipping recommit of identical chunk");
      return;
    }

    // If we've already started reading from chunk N+1 following this chunk N,
    // don't override chunk N. Otherwise we may end up reading a packet from
    // chunk N after having read from chunk N+1, thereby violating sequential
    // read of packets. This shouldn't happen if the producer is well-behaved,
    // because it shouldn't start chunk N+1 before completing chunk N.
    ChunkMeta::Key subsequent_key = key;
    static_assert(std::numeric_limits<ChunkID>::max() == kMaxChunkID,
                  "ChunkID wraps");
    subsequent_key.chunk_id++;
    const auto subsequent_it = index_.find(subsequent_key);
    if (subsequent_it != index_.end() &&
        subsequent_it->second.num_fragments_read > 0) {
      stats_.set_abi_violations(stats_.abi_violations() + 1);
      PERFETTO_DCHECK(suppress_client_dchecks_for_testing_);
      return;
    }

    // We should not have read past the last packet.
    if (record_meta->num_fragments_read > prev->num_fragments) {
      PERFETTO_ELOG(
          "TraceBufferV1 read too many fragments from an incomplete chunk");
      PERFETTO_DCHECK(suppress_client_dchecks_for_testing_);
      return;
    }

    uint8_t* wptr = reinterpret_cast<uint8_t*>(prev);
    TRACE_BUFFER_DLOG("  overriding chunk @ %" PRIdPTR ", size=%zu",
                      wptr - begin(), record_size);

    // Update chunk meta data stored in the index, as it may have changed.
    record_meta->num_fragments = num_fragments;
    record_meta->flags = chunk_flags;
    record_meta->set_complete(chunk_complete);

    // Override the ChunkRecord contents at the original |wptr|.
    TRACE_BUFFER_DLOG("  copying @ [%" PRIdPTR " - %" PRIdPTR "] %zu",
                      wptr - begin(), uintptr_t(wptr - begin()) + record_size,
                      record_size);
    WriteChunkRecord(wptr, record, src, size);
    TRACE_BUFFER_DLOG("Chunk raw: %s",
                      base::HexDump(wptr, record_size).c_str());
    stats_.set_chunks_rewritten(stats_.chunks_rewritten() + 1);
    return;
  }

  if (PERFETTO_UNLIKELY(discard_writes_))
    return DiscardWrite();

  // If there isn't enough room from the given write position. Write a padding
  // record to clear the end of the buffer and wrap back.
  const size_t cached_size_to_end = size_to_end();
  if (PERFETTO_UNLIKELY(record_size > cached_size_to_end)) {
    ssize_t res = DeleteNextChunksFor(cached_size_to_end);
    if (res == -1)
      return DiscardWrite();
    PERFETTO_DCHECK(static_cast<size_t>(res) <= cached_size_to_end);
    AddPaddingRecord(cached_size_to_end);
    wptr_ = begin();
    stats_.set_write_wrap_count(stats_.write_wrap_count() + 1);
    PERFETTO_DCHECK(size_to_end() >= record_size);
  }

  // At this point either |wptr_| points to an untouched part of the buffer
  // (i.e. *wptr_ == 0) or we are about to overwrite one or more ChunkRecord(s).
  // In the latter case we need to first figure out where the next valid
  // ChunkRecord is (if it exists) and add padding between the new record.
  // Example ((w) == write cursor):
  //
  // Initial state (wtpr_ == 0):
  // |0 (w)    |10               |30                  |50
  // +---------+-----------------+--------------------+--------------------+
  // | Chunk 1 | Chunk 2         | Chunk 3            | Chunk 4            |
  // +---------+-----------------+--------------------+--------------------+
  //
  // Let's assume we now want now write a 5th Chunk of size == 35. The final
  // state should look like this:
  // |0                                |35 (w)         |50
  // +---------------------------------+---------------+--------------------+
  // | Chunk 5                         | Padding Chunk | Chunk 4            |
  // +---------------------------------+---------------+--------------------+

  // Deletes all chunks from |wptr_| to |wptr_| + |record_size|.
  ssize_t del_res = DeleteNextChunksFor(record_size);
  if (del_res == -1)
    return DiscardWrite();
  size_t padding_size = static_cast<size_t>(del_res);

  // Now first insert the new chunk. At the end, if necessary, add the padding.
  stats_.set_chunks_written(stats_.chunks_written() + 1);
  stats_.set_bytes_written(stats_.bytes_written() + record_size);

  uint32_t chunk_off = GetOffset(GetChunkRecordAt(wptr_));
  auto it_and_inserted =
      index_.emplace(key, ChunkMeta(chunk_off, num_fragments, chunk_complete,
                                    chunk_flags, client_identity_trusted));
  PERFETTO_DCHECK(it_and_inserted.second);
  TRACE_BUFFER_DLOG("  copying @ [%" PRIdPTR " - %" PRIdPTR "] %zu",
                    wptr_ - begin(), uintptr_t(wptr_ - begin()) + record_size,
                    record_size);
  WriteChunkRecord(wptr_, record, src, size);
  TRACE_BUFFER_DLOG("Chunk raw: %s", base::HexDump(wptr_, record_size).c_str());
  wptr_ += record_size;
  if (wptr_ >= end()) {
    PERFETTO_DCHECK(padding_size == 0);
    wptr_ = begin();
    stats_.set_write_wrap_count(stats_.write_wrap_count() + 1);
  }
  DcheckIsAlignedAndWithinBounds(wptr_);

  // Chunks may be received out of order, so only update last_chunk_id if the
  // new chunk_id is larger. But take into account overflows by only selecting
  // the new ID if its distance to the latest ID is smaller than half the number
  // space.
  //
  // This accounts for both the case where the new ID has just overflown and
  // last_chunk_id be updated even though it's smaller (e.g. |chunk_id| = 1 and
  // |last_chunk_id| = kMaxChunkId; chunk_id - last_chunk_id = 0) and the case
  // where the new ID is an out-of-order ID right after an overflow and
  // last_chunk_id shouldn't be updated even though it's larger (e.g. |chunk_id|
  // = kMaxChunkId and |last_chunk_id| = 1; chunk_id - last_chunk_id =
  // kMaxChunkId - 1).
  auto producer_and_writer_id = std::make_pair(producer_id_trusted, writer_id);
  ChunkID& last_chunk_id = last_chunk_id_written_[producer_and_writer_id];
  static_assert(std::numeric_limits<ChunkID>::max() == kMaxChunkID,
                "This code assumes that ChunkID wraps at kMaxChunkID");
  if (chunk_id - last_chunk_id < kMaxChunkID / 2) {
    last_chunk_id = chunk_id;
  } else {
    stats_.set_chunks_committed_out_of_order(
        stats_.chunks_committed_out_of_order() + 1);
  }

  if (padding_size)
    AddPaddingRecord(padding_size);
}

ssize_t TraceBufferV1::DeleteNextChunksFor(size_t bytes_to_clear) {
  PERFETTO_CHECK(!discard_writes_);

  // Find the position of the first chunk which begins at or after
  // (|wptr_| + |bytes|). Note that such a chunk might not exist and we might
  // either reach the end of the buffer or a zeroed region of the buffer.
  uint8_t* next_chunk_ptr = wptr_;
  uint8_t* search_end = wptr_ + bytes_to_clear;
  TRACE_BUFFER_DLOG("Delete [%zu %zu]", wptr_ - begin(), search_end - begin());
  DcheckIsAlignedAndWithinBounds(wptr_);
  PERFETTO_DCHECK(search_end <= end());
  std::vector<ChunkMap::iterator> index_delete;
  uint64_t chunks_overwritten = stats_.chunks_overwritten();
  uint64_t bytes_overwritten = stats_.bytes_overwritten();
  uint64_t padding_bytes_cleared = stats_.padding_bytes_cleared();
  while (next_chunk_ptr < search_end) {
    const ChunkRecord& next_chunk = *GetChunkRecordAt(next_chunk_ptr);
    TRACE_BUFFER_DLOG(
        "  scanning chunk [%zu %zu] (valid=%d)", next_chunk_ptr - begin(),
        next_chunk_ptr - begin() + next_chunk.size, next_chunk.is_valid());

    // We just reached the untouched part of the buffer, it's going to be all
    // zeroes from here to end().
    // Optimization: if during Initialize() we fill the buffer with padding
    // records we could get rid of this branch.
    if (PERFETTO_UNLIKELY(!next_chunk.is_valid())) {
      // This should happen only at the first iteration. The zeroed area can
      // only begin precisely at the |wptr_|, not after. Otherwise it means that
      // we wrapped but screwed up the ChunkRecord chain.
      PERFETTO_DCHECK(next_chunk_ptr == wptr_);
      return 0;
    }

    // Remove |next_chunk| from the index, unless it's a padding record (padding
    // records are not part of the index).
    if (PERFETTO_LIKELY(!next_chunk.is_padding)) {
      ChunkMeta::Key key(next_chunk);
      auto it = index_.find(key);
      bool will_remove = false;
      if (PERFETTO_LIKELY(it != index_.end())) {
        const ChunkMeta& meta = it->second;
        if (PERFETTO_UNLIKELY(meta.num_fragments_read < meta.num_fragments)) {
          if (overwrite_policy_ == kDiscard)
            return -1;
          chunks_overwritten++;
          bytes_overwritten += next_chunk.size;
        }
        index_delete.push_back(it);
        will_remove = true;
      }
      TRACE_BUFFER_DLOG("  del index {%" PRIu32 ",%" PRIu32 ",%u} @ [%" PRIdPTR
                        " - %" PRIdPTR "] %d",
                        key.producer_id, key.writer_id, key.chunk_id,
                        next_chunk_ptr - begin(),
                        next_chunk_ptr - begin() + next_chunk.size,
                        will_remove);
      PERFETTO_DCHECK(will_remove);
    } else {
      padding_bytes_cleared += next_chunk.size;
    }

    next_chunk_ptr += next_chunk.size;

    // We should never hit this, unless we managed to screw up while writing
    // to the buffer and breaking the ChunkRecord(s) chain.
    // TODO(primiano): Write more meaningful logging with the status of the
    // buffer, to get more actionable bugs in case we hit this.
    PERFETTO_CHECK(next_chunk_ptr <= end());
  }

  // Remove from the index.
  for (auto it : index_delete) {
    index_.erase(it);
  }
  stats_.set_chunks_overwritten(chunks_overwritten);
  stats_.set_bytes_overwritten(bytes_overwritten);
  stats_.set_padding_bytes_cleared(padding_bytes_cleared);

  PERFETTO_DCHECK(next_chunk_ptr >= search_end && next_chunk_ptr <= end());
  return static_cast<ssize_t>(next_chunk_ptr - search_end);
}

void TraceBufferV1::AddPaddingRecord(size_t size) {
  PERFETTO_DCHECK(size >= sizeof(ChunkRecord) && size <= ChunkRecord::kMaxSize);
  ChunkRecord record(size);
  record.is_padding = 1;
  TRACE_BUFFER_DLOG("AddPaddingRecord @ [%" PRIdPTR " - %" PRIdPTR "] %zu",
                    wptr_ - begin(), uintptr_t(wptr_ - begin()) + size, size);
  WriteChunkRecord(wptr_, record, nullptr, size - sizeof(ChunkRecord));
  stats_.set_padding_bytes_written(stats_.padding_bytes_written() + size);
  // |wptr_| is deliberately not advanced when writing a padding record.
}

bool TraceBufferV1::TryPatchChunkContents(ProducerID producer_id,
                                          WriterID writer_id,
                                          ChunkID chunk_id,
                                          const Patch* patches,
                                          size_t patches_size,
                                          bool other_patches_pending) {
  PERFETTO_CHECK(!read_only_);
  ChunkMeta::Key key(producer_id, writer_id, chunk_id);
  auto it = index_.find(key);
  if (it == index_.end()) {
    stats_.set_patches_failed(stats_.patches_failed() + 1);
    return false;
  }
  ChunkMeta& chunk_meta = it->second;

  // Check that the index is consistent with the actual ProducerID/WriterID
  // stored in the ChunkRecord.

  ChunkRecord* chunk_record = GetChunkRecordAt(begin() + chunk_meta.record_off);
  PERFETTO_DCHECK(ChunkMeta::Key(*chunk_record) == key);
  uint8_t* chunk_begin = reinterpret_cast<uint8_t*>(chunk_record);
  PERFETTO_DCHECK(chunk_begin >= begin());
  uint8_t* chunk_end = chunk_begin + chunk_record->size;
  PERFETTO_DCHECK(chunk_end <= end());
  uint8_t* payload_begin = chunk_begin + sizeof(ChunkRecord);
  const size_t payload_size = static_cast<size_t>(chunk_end - payload_begin);

  static_assert(Patch::kSize == SharedMemoryABI::kPacketHeaderSize,
                "Patch::kSize out of sync with SharedMemoryABI");

  for (size_t i = 0; i < patches_size; i++) {
    const size_t offset_untrusted = patches[i].offset_untrusted;
    if (payload_size < Patch::kSize ||
        offset_untrusted > payload_size - Patch::kSize) {
      // Either the IPC was so slow and in the meantime the writer managed to
      // wrap over |chunk_id| or the producer sent a malicious IPC.
      stats_.set_patches_failed(stats_.patches_failed() + 1);
      return false;
    }
    TRACE_BUFFER_DLOG("PatchChunk {%" PRIu32 ",%" PRIu32
                      ",%u} size=%zu @ %zu with {%02x %02x %02x %02x}",
                      producer_id, writer_id, chunk_id, chunk_end - chunk_begin,
                      offset_untrusted, patches[i].data[0], patches[i].data[1],
                      patches[i].data[2], patches[i].data[3]);
    uint8_t* dst = payload_begin + offset_untrusted;
    memcpy(dst, &patches[i].data[0], Patch::kSize);
  }
  TRACE_BUFFER_DLOG("Chunk raw (after patch): %s",
                    base::HexDump(chunk_begin, chunk_record->size).c_str());

  stats_.set_patches_succeeded(stats_.patches_succeeded() + patches_size);
  if (!other_patches_pending) {
    chunk_meta.flags &= ~kChunkNeedsPatching;
    chunk_record->flags = chunk_meta.flags & ChunkRecord::kFlagsBitMask;
  }
  return true;
}

void TraceBufferV1::BeginRead() {
  read_iter_ = GetReadIterForSequence(index_.begin());
#if PERFETTO_DCHECK_IS_ON()
  changed_since_last_read_ = false;
#endif
}

TraceBufferV1::SequenceIterator TraceBufferV1::GetReadIterForSequence(
    ChunkMap::iterator seq_begin) {
  SequenceIterator iter;
  iter.seq_begin = seq_begin;
  if (seq_begin == index_.end()) {
    iter.cur = iter.seq_end = index_.end();
    return iter;
  }

#if PERFETTO_DCHECK_IS_ON()
  // Either |seq_begin| is == index_.begin() or the item immediately before must
  // belong to a different {ProducerID, WriterID} sequence.
  if (seq_begin != index_.begin() && seq_begin != index_.end()) {
    auto prev_it = seq_begin;
    prev_it--;
    PERFETTO_DCHECK(
        seq_begin == index_.begin() ||
        std::tie(prev_it->first.producer_id, prev_it->first.writer_id) <
            std::tie(seq_begin->first.producer_id, seq_begin->first.writer_id));
  }
#endif

  // Find the first entry that has a greater {ProducerID, WriterID} (or just
  // index_.end() if we reached the end).
  ChunkMeta::Key key = seq_begin->first;  // Deliberate copy.
  key.chunk_id = kMaxChunkID;
  iter.seq_end = index_.upper_bound(key);
  PERFETTO_DCHECK(iter.seq_begin != iter.seq_end);

  // Now find the first entry between [seq_begin, seq_end) that is
  // > last_chunk_id_written_. This is where we the sequence will start (see
  // notes about wrapping of IDs in the header).
  auto producer_and_writer_id = std::make_pair(key.producer_id, key.writer_id);
  PERFETTO_DCHECK(last_chunk_id_written_.count(producer_and_writer_id));
  iter.wrapping_id = last_chunk_id_written_[producer_and_writer_id];
  key.chunk_id = iter.wrapping_id;
  iter.cur = index_.upper_bound(key);
  if (iter.cur == iter.seq_end)
    iter.cur = iter.seq_begin;
  return iter;
}

void TraceBufferV1::SequenceIterator::MoveNext() {
  // Stop iterating when we reach the end of the sequence.
  // Note: |seq_begin| might be == |seq_end|.
  if (cur == seq_end || cur->first.chunk_id == wrapping_id) {
    cur = seq_end;
    return;
  }

  // If the current chunk wasn't completed yet, we shouldn't advance past it as
  // it may be rewritten with additional packets.
  if (!cur->second.is_complete()) {
    cur = seq_end;
    return;
  }

  ChunkID last_chunk_id = cur->first.chunk_id;
  if (++cur == seq_end)
    cur = seq_begin;

  // There may be a missing chunk in the sequence of chunks, in which case the
  // next chunk's ID won't follow the last one's. If so, skip the rest of the
  // sequence. We'll return to it later once the hole is filled.
  if (last_chunk_id + 1 != cur->first.chunk_id)
    cur = seq_end;
}

bool TraceBufferV1::ReadNextTracePacket(
    TracePacket* packet,
    PacketSequenceProperties* sequence_properties,
    bool* previous_packet_on_sequence_dropped) {
  // Note: MoveNext() moves only within the next chunk within the same
  // {ProducerID, WriterID} sequence. Here we want to:
  // - return the next patched+complete packet in the current sequence, if any.
  // - return the first patched+complete packet in the next sequence, if any.
  // - return false if none of the above is found.
  TRACE_BUFFER_DLOG("ReadNextTracePacket()");

  // Just in case we forget to initialize these below.
  *sequence_properties = {0, ClientIdentity(), 0};
  *previous_packet_on_sequence_dropped = false;

  // At the start of each sequence iteration, we consider the last read packet
  // dropped. While iterating over the chunks in the sequence, we update this
  // flag based on our knowledge about the last packet that was read from each
  // chunk (|last_read_packet_skipped| in ChunkMeta).
  bool previous_packet_dropped = true;

#if PERFETTO_DCHECK_IS_ON()
  PERFETTO_DCHECK(!changed_since_last_read_);
#endif
  for (;; read_iter_.MoveNext()) {
    if (PERFETTO_UNLIKELY(!read_iter_.is_valid())) {
      // We ran out of chunks in the current {ProducerID, WriterID} sequence or
      // we just reached the index_.end().

      if (PERFETTO_UNLIKELY(read_iter_.seq_end == index_.end()))
        return false;

      // We reached the end of sequence, move to the next one.
      // Note: ++read_iter_.seq_end might become index_.end(), but
      // GetReadIterForSequence() knows how to deal with that.
      read_iter_ = GetReadIterForSequence(read_iter_.seq_end);
      PERFETTO_DCHECK(read_iter_.is_valid() && read_iter_.cur != index_.end());
      previous_packet_dropped = true;
    }

    ChunkMeta* chunk_meta = &*read_iter_;

    // If the chunk has holes that are awaiting to be patched out-of-band,
    // skip the current sequence and move to the next one.
    if (chunk_meta->flags & kChunkNeedsPatching) {
      read_iter_.MoveToEnd();
      continue;
    }

    const ProducerID trusted_producer_id = read_iter_.producer_id();
    const WriterID writer_id = read_iter_.writer_id();
    const ProducerAndWriterID producer_and_writer_id =
        MkProducerAndWriterID(trusted_producer_id, writer_id);
    const ClientIdentity& client_identity = chunk_meta->client_identity_trusted;

    // At this point we have a chunk in |chunk_meta| that has not been fully
    // read. We don't know yet whether we have enough data to read the full
    // packet (in the case it's fragmented over several chunks) and we are about
    // to find that out. Specifically:
    // A) If the first fragment is unread and is a fragment continuing from a
    //    previous chunk, it means we have missed the previous ChunkID. In
    //    fact, if this wasn't the case, a previous call to ReadNext() shouldn't
    //    have moved the cursor to this chunk.
    // B) Any fragment > 0 && < last is always readable. By definition an inner
    //    packet is never fragmented and hence doesn't require neither stitching
    //    nor any out-of-band patching. The same applies to the last packet
    //    iff it doesn't continue on the next chunk.
    // C) If the last packet (which might be also the only packet in the chunk)
    //    is a fragment and continues on the next chunk, we peek at the next
    //    chunks and, if we have all of them, mark as read and move the cursor.
    //
    // +---------------+   +-------------------+  +---------------+
    // | ChunkID: 1    |   | ChunkID: 2        |  | ChunkID: 3    |
    // |---------------+   +-------------------+  +---------------+
    // | Packet 1      |   |                   |  | ... Packet 3  |
    // | Packet 2      |   | ... Packet 3  ... |  | Packet 4      |
    // | Packet 3  ... |   |                   |  | Packet 5 ...  |
    // +---------------+   +-------------------+  +---------------+

    PERFETTO_DCHECK(chunk_meta->num_fragments_read <=
                    chunk_meta->num_fragments);

    // If we didn't read any packets from this chunk, the last packet was from
    // the previous chunk we iterated over; so don't update
    // |previous_packet_dropped| in this case.
    if (chunk_meta->num_fragments_read > 0)
      previous_packet_dropped = chunk_meta->last_read_packet_skipped();

    while (chunk_meta->num_fragments_read < chunk_meta->num_fragments) {
      enum { kSkip = 0, kReadOnePacket, kTryReadAhead } action;
      if (chunk_meta->num_fragments_read == 0) {
        if (chunk_meta->flags & kFirstPacketContinuesFromPrevChunk) {
          action = kSkip;  // Case A.
        } else if (chunk_meta->num_fragments == 1 &&
                   (chunk_meta->flags & kLastPacketContinuesOnNextChunk)) {
          action = kTryReadAhead;  // Case C.
        } else {
          action = kReadOnePacket;  // Case B.
        }
      } else if (chunk_meta->num_fragments_read <
                     chunk_meta->num_fragments - 1 ||
                 !(chunk_meta->flags & kLastPacketContinuesOnNextChunk)) {
        action = kReadOnePacket;  // Case B.
      } else {
        action = kTryReadAhead;  // Case C.
      }

      TRACE_BUFFER_DLOG("  chunk %u, packet %hu of %hu, action=%d",
                        read_iter_.chunk_id(), chunk_meta->num_fragments_read,
                        chunk_meta->num_fragments, action);

      if (action == kSkip) {
        // This fragment will be skipped forever, not just in this ReadPacket()
        // iteration. This happens by virtue of ReadNextPacketInChunk()
        // incrementing the |num_fragments_read| and marking the fragment as
        // read even if we didn't really.
        ReadNextPacketInChunk(producer_and_writer_id, chunk_meta, nullptr);
        chunk_meta->set_last_read_packet_skipped(true);
        previous_packet_dropped = true;
        continue;
      }

      if (action == kReadOnePacket) {
        // The easy peasy case B.
        ReadPacketResult result =
            ReadNextPacketInChunk(producer_and_writer_id, chunk_meta, packet);

        if (PERFETTO_LIKELY(result == ReadPacketResult::kSucceeded)) {
          *sequence_properties = {trusted_producer_id, client_identity,
                                  writer_id};
          *previous_packet_on_sequence_dropped = previous_packet_dropped;
          return true;
        } else if (result == ReadPacketResult::kFailedEmptyPacket) {
          // We can ignore and skip empty packets.
          PERFETTO_DCHECK(packet->slices().empty());
          continue;
        }

        // In extremely rare cases (producer bugged / malicious) the chunk might
        // contain an invalid fragment. In such case we don't want to stall the
        // sequence but just skip the chunk and move on. ReadNextPacketInChunk()
        // marks the chunk as fully read, so we don't attempt to read from it
        // again in a future call to ReadBuffers(). It also already records an
        // abi violation for this.
        PERFETTO_DCHECK(result == ReadPacketResult::kFailedInvalidPacket);
        chunk_meta->set_last_read_packet_skipped(true);
        previous_packet_dropped = true;
        break;
      }

      PERFETTO_DCHECK(action == kTryReadAhead);
      ReadAheadResult ra_res = ReadAhead(packet);
      if (ra_res == ReadAheadResult::kSucceededReturnSlices) {
        stats_.set_readaheads_succeeded(stats_.readaheads_succeeded() + 1);
        *sequence_properties = {trusted_producer_id, client_identity,
                                writer_id};
        *previous_packet_on_sequence_dropped = previous_packet_dropped;
        return true;
      }

      if (ra_res == ReadAheadResult::kFailedMoveToNextSequence) {
        // readahead didn't find a contiguous packet sequence. We'll try again
        // on the next ReadPacket() call.
        stats_.set_readaheads_failed(stats_.readaheads_failed() + 1);

        // TODO(primiano): optimization: this MoveToEnd() is the reason why
        // MoveNext() (that is called in the outer for(;;MoveNext)) needs to
        // deal gracefully with the case of |cur|==|seq_end|. Maybe we can do
        // something to avoid that check by reshuffling the code here?
        read_iter_.MoveToEnd();

        // This break will go back to beginning of the for(;;MoveNext()). That
        // will move to the next sequence because we set the read iterator to
        // its end.
        break;
      }

      PERFETTO_DCHECK(ra_res == ReadAheadResult::kFailedStayOnSameSequence);

      // In this case ReadAhead() might advance |read_iter_|, so we need to
      // re-cache the |chunk_meta| pointer to point to the current chunk.
      chunk_meta = &*read_iter_;
      chunk_meta->set_last_read_packet_skipped(true);
      previous_packet_dropped = true;
    }  // while(...)  [iterate over packet fragments for the current chunk].
  }  // for(;;MoveNext()) [iterate over chunks].
}

TraceBufferV1::ReadAheadResult TraceBufferV1::ReadAhead(TracePacket* packet) {
  static_assert(static_cast<ChunkID>(kMaxChunkID + 1) == 0,
                "relying on kMaxChunkID to wrap naturally");
  TRACE_BUFFER_DLOG(" readahead start @ chunk %u", read_iter_.chunk_id());
  ChunkID next_chunk_id = read_iter_.chunk_id() + 1;
  SequenceIterator it = read_iter_;
  for (it.MoveNext(); it.is_valid(); it.MoveNext(), next_chunk_id++) {
    // We should stay within the same sequence while iterating here.
    PERFETTO_DCHECK(it.producer_id() == read_iter_.producer_id() &&
                    it.writer_id() == read_iter_.writer_id());

    TRACE_BUFFER_DLOG("   expected chunk ID: %u, actual ID: %u", next_chunk_id,
                      it.chunk_id());

    if (PERFETTO_UNLIKELY((*it).num_fragments == 0))
      continue;

    // If we miss the next chunk, stop looking in the current sequence and
    // try another sequence. This chunk might come in the near future.
    // The second condition is the edge case of a buggy/malicious
    // producer. The ChunkID is contiguous but its flags don't make sense.
    if (it.chunk_id() != next_chunk_id ||
        PERFETTO_UNLIKELY(
            !((*it).flags & kFirstPacketContinuesFromPrevChunk))) {
      return ReadAheadResult::kFailedMoveToNextSequence;
    }

    // If the chunk is contiguous but has not been patched yet move to the next
    // sequence and try coming back here on the next ReadNextTracePacket() call.
    // TODO(primiano): add a test to cover this, it's a subtle case.
    if ((*it).flags & kChunkNeedsPatching)
      return ReadAheadResult::kFailedMoveToNextSequence;

    // This is the case of an intermediate chunk which contains only one
    // fragment which continues on the next chunk. This is the case for large
    // packets, e.g.: [Packet0, Packet1(0)] [Packet1(1)] [Packet1(2), ...]
    // (Packet1(X) := fragment X of Packet1).
    if ((*it).num_fragments == 1 &&
        ((*it).flags & kLastPacketContinuesOnNextChunk)) {
      continue;
    }

    // We made it! We got all fragments for the packet without holes.
    TRACE_BUFFER_DLOG("  readahead success @ chunk %u", it.chunk_id());
    PERFETTO_DCHECK(((*it).num_fragments == 1 &&
                     !((*it).flags & kLastPacketContinuesOnNextChunk)) ||
                    (*it).num_fragments > 1);

    // Now let's re-iterate over the [read_iter_, it] sequence and mark
    // all the fragments as read.
    bool packet_corruption = false;
    for (;;) {
      PERFETTO_DCHECK(read_iter_.is_valid());
      TRACE_BUFFER_DLOG("    commit chunk %u", read_iter_.chunk_id());
      if (PERFETTO_LIKELY((*read_iter_).num_fragments > 0)) {
        // In the unlikely case of a corrupted packet (corrupted or empty
        // fragment), invalidate the all stitching and move on to the next chunk
        // in the same sequence, if any.
        auto pw_id = MkProducerAndWriterID(it.producer_id(), it.writer_id());
        packet_corruption |=
            ReadNextPacketInChunk(pw_id, &*read_iter_, packet) ==
            ReadPacketResult::kFailedInvalidPacket;
      }
      if (read_iter_.cur == it.cur)
        break;
      read_iter_.MoveNext();
    }  // for(;;)
    PERFETTO_DCHECK(read_iter_.cur == it.cur);

    if (PERFETTO_UNLIKELY(packet_corruption)) {
      // ReadNextPacketInChunk() already records an abi violation for this case.
      *packet = TracePacket();  // clear.
      return ReadAheadResult::kFailedStayOnSameSequence;
    }

    return ReadAheadResult::kSucceededReturnSlices;
  }  // for(it...)  [readahead loop]
  return ReadAheadResult::kFailedMoveToNextSequence;
}

TraceBufferV1::ReadPacketResult TraceBufferV1::ReadNextPacketInChunk(
    ProducerAndWriterID producer_and_writer_id,
    ChunkMeta* const chunk_meta,
    TracePacket* packet) {
  PERFETTO_DCHECK(chunk_meta->num_fragments_read < chunk_meta->num_fragments);
  PERFETTO_DCHECK(!(chunk_meta->flags & kChunkNeedsPatching));

  const uint8_t* record_begin = begin() + chunk_meta->record_off;
  DcheckIsAlignedAndWithinBounds(record_begin);
  auto* chunk_record = reinterpret_cast<const ChunkRecord*>(record_begin);
  const uint8_t* record_end = record_begin + chunk_record->size;
  const uint8_t* packets_begin = record_begin + sizeof(ChunkRecord);
  const uint8_t* packet_begin = packets_begin + chunk_meta->cur_fragment_offset;

  if (PERFETTO_UNLIKELY(packet_begin < packets_begin ||
                        packet_begin >= record_end)) {
    // The producer has a bug or is malicious and did declare that the chunk
    // contains more packets beyond its boundaries.
    stats_.set_abi_violations(stats_.abi_violations() + 1);
    PERFETTO_DCHECK(suppress_client_dchecks_for_testing_);
    chunk_meta->cur_fragment_offset = 0;
    chunk_meta->num_fragments_read = chunk_meta->num_fragments;
    if (PERFETTO_LIKELY(chunk_meta->is_complete())) {
      stats_.set_chunks_read(stats_.chunks_read() + 1);
      stats_.set_bytes_read(stats_.bytes_read() + chunk_record->size);
    }
    return ReadPacketResult::kFailedInvalidPacket;
  }

  // A packet (or a fragment) starts with a varint stating its size, followed
  // by its content. The varint shouldn't be larger than 4 bytes (just in case
  // the producer is using a redundant encoding)
  uint64_t packet_size = 0;
  const uint8_t* header_end =
      std::min(packet_begin + protozero::proto_utils::kMessageLengthFieldSize,
               record_end);
  const uint8_t* packet_data = protozero::proto_utils::ParseVarInt(
      packet_begin, header_end, &packet_size);

  const uint8_t* next_packet = packet_data + packet_size;
  if (PERFETTO_UNLIKELY(next_packet <= packet_begin ||
                        next_packet > record_end)) {
    // In BufferExhaustedPolicy::kDrop mode, TraceWriter may abort a fragmented
    // packet by writing an invalid size in the last fragment's header. We
    // should handle this case without recording an ABI violation (since Android
    // R).
    if (packet_size != SharedMemoryABI::kPacketSizeDropPacket) {
      stats_.set_abi_violations(stats_.abi_violations() + 1);
      PERFETTO_DCHECK(suppress_client_dchecks_for_testing_);
    } else {
      stats_.set_trace_writer_packet_loss(stats_.trace_writer_packet_loss() +
                                          1);
    }
    chunk_meta->cur_fragment_offset = 0;
    chunk_meta->num_fragments_read = chunk_meta->num_fragments;
    if (PERFETTO_LIKELY(chunk_meta->is_complete())) {
      stats_.set_chunks_read(stats_.chunks_read() + 1);
      stats_.set_bytes_read(stats_.bytes_read() + chunk_record->size);
    }
    return ReadPacketResult::kFailedInvalidPacket;
  }

  chunk_meta->cur_fragment_offset =
      static_cast<uint16_t>(next_packet - packets_begin);
  chunk_meta->num_fragments_read++;

  if (PERFETTO_UNLIKELY(chunk_meta->num_fragments_read ==
                            chunk_meta->num_fragments &&
                        chunk_meta->is_complete())) {
    stats_.set_chunks_read(stats_.chunks_read() + 1);
    stats_.set_bytes_read(stats_.bytes_read() + chunk_record->size);
    writer_stats_.Insert(producer_and_writer_id,
                         chunk_meta->cur_fragment_offset);
  } else {
    // We have at least one more packet to parse. It should be within the chunk.
    if (chunk_meta->cur_fragment_offset + sizeof(ChunkRecord) >=
        chunk_record->size) {
      PERFETTO_DCHECK(suppress_client_dchecks_for_testing_);
    }
  }

  chunk_meta->set_last_read_packet_skipped(false);

  if (PERFETTO_UNLIKELY(packet_size == 0))
    return ReadPacketResult::kFailedEmptyPacket;

  if (PERFETTO_LIKELY(packet))
    packet->AddSlice(packet_data, static_cast<size_t>(packet_size));

  return ReadPacketResult::kSucceeded;
}

void TraceBufferV1::DiscardWrite() {
  PERFETTO_DCHECK(overwrite_policy_ == kDiscard);
  discard_writes_ = true;
  stats_.set_chunks_discarded(stats_.chunks_discarded() + 1);
  TRACE_BUFFER_DLOG("  discarding write");
}

std::unique_ptr<TraceBuffer> TraceBufferV1::CloneReadOnly() const {
  std::unique_ptr<TraceBufferV1> buf(new TraceBufferV1(CloneCtor(), *this));
  if (!buf->data_.IsValid())
    return nullptr;  // PagedMemory::Allocate() failed. We are out of memory.
  return buf;
}

TraceBufferV1::TraceBufferV1(CloneCtor, const TraceBufferV1& src)
    : overwrite_policy_(src.overwrite_policy_),
      read_only_(true),
      discard_writes_(src.discard_writes_) {
  if (!Initialize(src.data_.size()))
    return;  // TraceBuffer::Clone() will check |data_| and return nullptr.

  // The assignments below must be done after Initialize().

  EnsureCommitted(src.used_size_);
  memcpy(data_.Get(), src.data_.Get(), src.used_size_);
  last_chunk_id_written_ = src.last_chunk_id_written_;

  stats_ = src.stats_;
  stats_.set_bytes_read(0);
  stats_.set_chunks_read(0);
  stats_.set_readaheads_failed(0);
  stats_.set_readaheads_succeeded(0);

  // Copy the index of chunk metadata.
  // NOTE: in 2025-10 the behavior of CloneReadOnly() in presence of existing
  // reads has changed.
  // Before: the read iterator would be reset and the cloned buffer would behave
  //         as if no read was performed.
  // After: the read iterators are untouched.
  // This is done for two reasons:
  // 1. To converge with the behavior of TraceBufferV2, which can't support the
  //    old behavior.
  // 2. To support properly cloning of write_into_file sessions (b/382209797).
  index_ = ChunkMap(src.index_);
  if constexpr (!PERFETTO_FLAGS(BUFFER_CLONE_PRESERVE_READ_ITER)) {
    for (auto& kv : index_) {
      ChunkMeta& chunk_meta = kv.second;
      chunk_meta.num_fragments_read = 0;
      chunk_meta.cur_fragment_offset = 0;
      chunk_meta.set_last_read_packet_skipped(false);
    }
  }
  read_iter_ = SequenceIterator();
}

// For the virtual base class.
TraceBuffer::~TraceBuffer() = default;

}  // namespace perfetto
