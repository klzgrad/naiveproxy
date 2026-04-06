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

#include "src/tracing/service/trace_buffer_v2.h"

#include <algorithm>
#include <memory>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/core/client_identity.h"
#include "perfetto/ext/tracing/core/shared_memory_abi.h"
#include "perfetto/protozero/proto_utils.h"
#include "src/protovm/vm.h"

// Set manually when debugging test failures.
// TRACE_BUFFER_V2_DLOG is too verbose, even for debug builds.
#define TRACE_BUFFER_V2_VERBOSE_LOGGING() 0

#if TRACE_BUFFER_V2_VERBOSE_LOGGING()
#define TRACE_BUFFER_V2_DLOG PERFETTO_DLOG
#else
#define TRACE_BUFFER_V2_DLOG(...) base::ignore_result(__VA_ARGS__)
#endif

using protozero::proto_utils::ParseVarInt;
namespace proto_utils = ::protozero::proto_utils;

namespace perfetto {

namespace {

// The names below need some mangling because v1/v2.cc collide in the
// amalgamated builds.
constexpr uint8_t kFirstPacketContFromPrevChunk =
    SharedMemoryABI::ChunkHeader::kFirstPacketContinuesFromPrevChunk;
constexpr uint8_t kLastPacketContOnNextChunk =
    SharedMemoryABI::ChunkHeader::kLastPacketContinuesOnNextChunk;
constexpr uint8_t kChunkNeedsPatch =
    SharedMemoryABI::ChunkHeader::kChunkNeedsPatching;

// The only thing that causes incomplete-ness is SMB scraping, when calling
// CopyChunkUntrusted(...chunk_complete=false). This has nothing to do with
// patching.
constexpr uint8_t kChunkIncomplete = 0x80;

// Mask out the flags that don't come from the ABI like kChunkIncomplete.
constexpr uint8_t kFlagsMask = SharedMemoryABI::ChunkHeader::kFlagsMask;

// Compares two ChunkID(s) in a wrapping 32-bit ID space.
// Returns:
//   -1 if a comes before b in modular (wrapping) order.
//    0 if a == b
//   +1 if a comes after b in modular order.
//
// The key idea is that the order between two distinct IDs is determined by
// whether the distance from a to b is less than 2^31 (half the range).
// Many other TCP/IP stacks do the same, e.g.
// https://github.com/openbsd/src/blob/master/sys/netinet/tcp_seq.h#L43
int ChunkIdCompare(ChunkID a, ChunkID b) {
  if (a == b)
    return 0;
  return (static_cast<int32_t>(a - b) < 0) ? -1 : 1;
}

// The number of the latest empty SequenceState(s) to keep around.
constexpr size_t kKeepLastEmptySeq = 1024;

// The threshold when we start scanning and deleting the oldest sequences.
constexpr size_t kEmptySequencesGcTreshold = kKeepLastEmptySeq + 128;
}  // namespace.

namespace internal {

SequenceState::SequenceState(ProducerID p, WriterID w, ClientIdentity c)
    : producer_id(p),
      writer_id(w),
      client_identity(c),
      chunks(/*initial_capacity=*/64) {}
SequenceState::~SequenceState() = default;
SequenceState::SequenceState(const SequenceState&) noexcept = default;
SequenceState& SequenceState::operator=(const SequenceState&) noexcept =
    default;

// +---------------------------------------------------------------------------+
// | FragIterator                                                              |
// +---------------------------------------------------------------------------+

std::optional<Frag> FragIterator::NextFragmentInChunk() {
  // This function must always visit all the fragments without ever returning
  // early. If some early return is needed, it must be done by the caller.
  PERFETTO_DCHECK(next_frag_off_ <= chunk_size_);
  const uint8_t* chunk_end = chunk_begin_ + chunk_size_;
  const uint8_t* hdr_begin = chunk_begin_ + next_frag_off_;
  if (hdr_begin >= chunk_end)
    return std::nullopt;

  bool is_first_frag = next_frag_off_ == 0;
  uint64_t frag_size_u64 = 0;

  // The structure of a fragment is the following:
  // [ varint header ] [ payload                       ]
  // frag_begin:  points at the beginning of the payload.
  // hdr_size     is the size in bytes of the varint header (1 or more bytes).
  // frag_size    is the size of the payload, without counting the header.
  uint8_t* frag_begin =
      const_cast<uint8_t*>(ParseVarInt(hdr_begin, chunk_end, &frag_size_u64));
  uint8_t hdr_size =  // The fragment header is just a varint stating its size.
      static_cast<uint8_t>(reinterpret_cast<uintptr_t>(frag_begin) -
                           reinterpret_cast<uintptr_t>(hdr_begin));

  // If the varint header is too long (e.g. ff ff .. ff), ParseVarInt will bail
  // will out after 10 bytes.
  if (PERFETTO_UNLIKELY(hdr_size == 0)) {
    chunk_corrupted_ = true;
    return std::nullopt;
  }

  // In BufferExhaustedPolicy::kDrop mode, Since Android R, TraceWriter may
  // abort a fragmented packet by writing an invalid size in the last
  // fragment's header. We should handle this case without recording an ABI
  // violation.
  if (PERFETTO_UNLIKELY(frag_size_u64 ==
                        SharedMemoryABI::kPacketSizeDropPacket)) {
    trace_writer_data_drop_ = true;
    return std::nullopt;
  }

  // We don't need to do anything special about padding chunks, because their
  // payload_avail is always 0. We naturally return nullopt on the check below.
  if (frag_size_u64 > reinterpret_cast<uintptr_t>(chunk_end) -
                          reinterpret_cast<uintptr_t>(frag_begin)) {
    chunk_corrupted_ = true;
    return std::nullopt;
  }

  uint16_t frag_size = static_cast<uint16_t>(frag_size_u64);
  next_frag_off_ += hdr_size + frag_size;  // Was: next_frag_off_ += frag_size;
  bool is_last_frag = next_frag_off_ >= chunk_size_;
  bool first_frag_continues = chunk_flags_ & kFirstPacketContFromPrevChunk;
  bool last_frag_continues = chunk_flags_ & kLastPacketContOnNextChunk;

  Frag::FragType frag_type = Frag::kFragWholePacket;
  if (is_last_frag && last_frag_continues) {
    if (is_first_frag && first_frag_continues) {
      frag_type = Frag::kFragContinue;
    } else {
      frag_type = Frag::kFragBegin;
    }
  } else if (is_first_frag && first_frag_continues) {
    frag_type = Frag::kFragEnd;
  } else {
    frag_type = Frag::kFragWholePacket;
  }
  return Frag(frag_begin, frag_type, hdr_size, frag_size);
}

// +---------------------------------------------------------------------------+
// | ChunkSeqIterator                                                          |
// +---------------------------------------------------------------------------+

ChunkSeqIterator::ChunkSeqIterator(TraceBufferV2* buf, SequenceState* seq)
    : buf_(buf), seq_(seq) {
  auto& chunk_list = seq_->chunks;
  PERFETTO_CHECK(!chunk_list.empty());
  size_t first_off = *chunk_list.begin();
  TBChunk* first_chunk_of_seq = buf->GetTBChunkAt(first_off);
  PERFETTO_DCHECK(!first_chunk_of_seq->is_padding());
  chunk_ = first_chunk_of_seq;
}

TBChunk* ChunkSeqIterator::NextChunkInSequence() {
  PERFETTO_DCHECK(seq_);
  auto& chunk_list = seq_->chunks;

  // Either the current chunk has been deleted (is_padding()), or if it exist it
  // must be consistent with the internal tracking in list_idx_.
  PERFETTO_DCHECK(chunk_->is_padding() ||
                  chunk_list.at(list_idx_) == buf_->OffsetOf(chunk_));

  size_t next_list_idx = list_idx_ + 1;
  if (next_list_idx >= chunk_list.size()) {
    // There is no "next chunk" the chunk list for this sequence.
    // NOTE: this has nothing to do with the "ChunkID is not consecutive" check
    // which is performed below. This is a more basic failure mode where we just
    // don't have any chunks at all, whether they are consecutive or not.
    return nullptr;
  }

  // At this point we need to work out if the sequence of ChunkID(s) is
  // contiguous or we have gaps. There are two scenarios here:
  // 1) We are iterating and consuming as part of ReadNextTracePacket(). When
  //    we do this, each iteration erases the last chunk before moving onto the
  //    next one. We can't use the SequenceState.chunks because the upcoming
  //    chunk will always be the "first" in this case. However, we can look at
  //    last_chunk_id_consumed.
  // 2) We are iterating read-only as part of ReassembleFragmentedPacket(). In
  //    this case we are not consuming any chunk, and we can use the combination
  //    of SequenceState.chunks[SequenceState.next_seq_idx].
  std::optional<ChunkID> last_chunk_id;
  if (chunk_->is_padding()) {
    last_chunk_id = seq_->last_chunk_id_consumed;  // Case 1.
  } else {
    last_chunk_id = chunk_->chunk_id;  // Case 2
  }

  size_t next_chunk_off = chunk_list.at(next_list_idx);      // O(1)
  TBChunk* next_chunk = buf_->GetTBChunkAt(next_chunk_off);  // O(1)

  if (last_chunk_id.has_value() && next_chunk->chunk_id != *last_chunk_id + 1)
    sequence_gap_detected_ = true;

  chunk_ = next_chunk;
  list_idx_ = next_list_idx;
  return next_chunk;
}

void ChunkSeqIterator::EraseCurrentChunk() {
  size_t chunk_off = buf_->OffsetOf(chunk_);
  TRACE_BUFFER_V2_DLOG("EraseChunk() id=%u off=%zu", chunk_->chunk_id,
                       chunk_off);
  const uint32_t chunk_size = chunk_->size;
  seq_->last_chunk_id_consumed = chunk_->chunk_id;

  auto& chunk_list = seq_->chunks;

  // At the time of writing we only support erasing the first chunk of the
  // sequence. Erasing from the middle is possible but requires more efforts
  // to keep in sync the SequenceState.chunks with our list_idx_.
  PERFETTO_CHECK(list_idx_ == 0 && *chunk_list.begin() == chunk_off);
  chunk_list.pop_front();
  if (chunk_list.empty()) {
    seq_->age_for_gc = ++buf_->seq_age_;
    ++buf_->empty_sequences_;
  }

  // list_idx_ points to the current index, but we just deleted it.
  // Point it to -1, so the next time that is incremented it goes back to the
  // 0th element, which is the one after the one being deleted.
  list_idx_ = static_cast<size_t>(-1);

  // Zero all the fields of the chunk.
  uint16_t old_payload_size = chunk_->payload_size;
  TBChunk* cleared_chunk = buf_->CreateTBChunk(chunk_off, chunk_size);
  cleared_chunk->payload_size = old_payload_size;

  // NOTE Stats are NOT updated here. The right place to update stat is in the
  // callers, specifically DeleteNextChunksFor (when we overwrite) and
  // ConsumeFragment (when we read).
}

// +---------------------------------------------------------------------------+
// | ChunkSeqReader                                                            |
// +---------------------------------------------------------------------------+

ChunkSeqReader::ChunkSeqReader(TraceBufferV2* buf,
                               TBChunk* end_chunk,
                               Mode mode)
    : buf_(buf),
      mode_(mode),
      end_(end_chunk),
      seq_(buf_->GetSeqForChunk(end_chunk)),
      seq_iter_(buf, seq_),
      iter_(seq_iter_.chunk()),  // Points to the first chunk of the sequence.
      frag_iter_(iter_) {
  PERFETTO_DCHECK(!iter_->is_padding());
  TRACE_BUFFER_V2_DLOG("ChunkSeqReader() last_id=%u iter_=%u end_=%u",
                       seq_->last_chunk_id_consumed.value_or(0),
                       iter_->chunk_id, end_->chunk_id);

  if (seq_->last_chunk_id_consumed.has_value() &&
      iter_->chunk_id != *seq_->last_chunk_id_consumed + 1) {
    seq_->data_loss = true;
  }
}

bool ChunkSeqReader::ReadNextPacketInSeqOrder(TracePacket* out_packet) {
  PERFETTO_DCHECK(!iter_->is_padding());
  PERFETTO_DCHECK(frag_iter_.next_frag_off() >= iter_->unread_payload_off());
  PERFETTO_DCHECK(frag_iter_.next_frag_off() <= iter_->payload_size);

  // skip_in_generation is set when we detect a situation that requires
  // "stalling" (i.e. skipping) the sequence: a fragment with no further data;
  // a kNeedsPatching chunk; a kChunkIncomplete.
  if (seq_->skip_in_generation == buf_->read_generation_ && mode_ == kReadMode)
    return false;

  // It is very important that this loop terminates only after having visited
  // all the fragments of all the chunks in the sequence up to `end_` (which is
  // the chunk passed initially to the ChunkSeqReader's ctor).
  // This is because this class is used both while doing readbacks and,
  // importantly, in DeleteNextChunksFor() when overwriting.
  for (;;) {
    std::optional<Frag> maybe_frag = frag_iter_.NextFragmentInChunk();
    if (!maybe_frag.has_value()) {
      // Once we exhaust all fragments, move to the next chunk in the sequence.
      bool end_reached = iter_ == end_;

      if (frag_iter_.chunk_corrupted()) {
        seq_->data_loss = true;
      }

      // If a chunk is incomplete, this is the point where we stop processing
      // the sequence (unless it is a deletion, in which case, complete or not,
      // we have to go over it anyways because we need space for a new chunk).
      if ((iter_->flags & kChunkIncomplete) && mode_ == kReadMode) {
        // If the chunk is incomplete, we don't want to read past it.
        seq_->skip_in_generation = buf_->read_generation_;
        end_reached = true;
      } else {
        // We read all the fragments for the current chunk. Erase it.
        seq_iter_.EraseCurrentChunk();
      }
      if (end_reached)
        return false;  // We reached our target chunk (or an incomplete chunk).
      TBChunk* next_chunk = seq_iter_.NextChunkInSequence();
      if (!next_chunk)
        return false;  // There are no more chunks in the sequence.
      iter_ = next_chunk;
      frag_iter_ = FragIterator(next_chunk);
      continue;
    }

    Frag& frag = *maybe_frag;
    switch (frag.type) {
      case Frag::kFragWholePacket:
        ConsumeFragment(iter_, &frag);
        // It's questionable whether we should propagate out empty packets. Here
        // we match the behavior of the legacy TraceBufferV1. Some clients might
        // be relying on the fact that empty packets don't bloat the final trace
        // file size.
        if (frag.size == 0) {
          break;  // Breaks the switch(), continues the loop.
        }
        if (out_packet)
          out_packet->AddSlice(frag.begin, frag.size);
        TRACE_BUFFER_V2_DLOG(
            "  ReadNextPacketInSeqOrder -> true (whole packet)");
        return true;

      case Frag::kFragContinue:
      case Frag::kFragEnd:
        // We should never hit these cases while iterating in this loop.
        // In nominal conditions we should only see kFragBegin, and then we
        // should iterate over the Continue/End in ReassembleFragmentedPacket(),
        // which performs the lookahead. If we hit this code path, either a
        // producer emitted a chunk sequence like [kWholePacket],[kFragEnd]
        // or, more realistically, we had a data losss and missed the chunk with
        // the kFragBegin.
        seq_->data_loss = true;
        ConsumeFragment(iter_, &frag);
        break;  // Break the switch, continue the loop.

      case Frag::kFragBegin:
        auto reassembly_res = ReassembleFragmentedPacket(out_packet, &frag);
        if (reassembly_res == FragReassemblyResult::kSuccess) {
          buf_->stats_.set_readaheads_succeeded(
              buf_->stats_.readaheads_succeeded() + 1);

          // We found and consumed all the fragments for the packet.
          // On the next ReadNextTracePacket() call, NextFragmentInChunk() will
          // return nullopt (because, modulo client bugs, the kFragBegin here
          // is the last fragment of the chunk). That code branch above will
          // erase the chunk and continue with the next chunk (either in buffer
          // or sequence order).
          TRACE_BUFFER_V2_DLOG(
              "  ReadNextPacketInSeqOrder -> true (reassembly)");
          return true;
        }

        // If we get here reassembly_res is either kNotEnoughData or kDataLoss.

        buf_->stats_.set_readaheads_failed(buf_->stats_.readaheads_failed() +
                                           1);

        if (reassembly_res == FragReassemblyResult::kNotEnoughData &&
            mode_ == kReadMode) {
          // If we got no more chunks, there is no point insisting with this
          // chunk, give up and let the caller try other chunks in buffer order.
          // Note that if we get this, it does NOT mean there are no other
          // chunks next in the sequence. We might have hit a "needs patching"
          // chunk.
          seq_->skip_in_generation = buf_->read_generation_;
          return false;
          // If mode_ == kEraseMode, we want to continue the loop and let the
          // prologue of the loop EraseCurrentChunk().
        }

        // If we get here either we have a kDataLoss, or
        // kNotEnoughData && mode_ == kEraseMode.
        // In either case we want to continue the loop and let the prologue of
        // the next loop iteration do EraseCurrentChunk().
        PERFETTO_DCHECK(
            reassembly_res == FragReassemblyResult::kDataLoss ||
            (reassembly_res == FragReassemblyResult::kNotEnoughData &&
             mode_ == kEraseMode));

        // If we detect a data loss, ReassembleFragmentedPacket() consumes all
        // fragments up until the discontinuity point, so they don't trigger
        // further error stats when we iterate over them again.
        // The break below will continue with the next chunk in sequence, if
        // any. Keep this case in mind here:
        // - We start the read cycle
        // - On the first chunk we find there are prior in-sequence chunks,
        //   so we rewind (we go back on the sequence's chunk list).
        // - Then we find that, in this sequence, there is a "broken" packet.
        // We should keep going on the same sequence and mark the data loss.
        seq_->data_loss = true;
        break;  // case kFragBegin
    }  // switch(frag.type)
  }  // for(;;)
}

void ChunkSeqReader::ConsumeFragment(TBChunk* chunk, Frag* frag) {
  // We must consume fragments in order (and no more than once).
  uintptr_t payload_addr = reinterpret_cast<uintptr_t>(frag->begin);
  uintptr_t chunk_addr = reinterpret_cast<uintptr_t>(chunk->fragments_begin());
  PERFETTO_DCHECK(payload_addr > chunk_addr);
  uintptr_t payload_off = payload_addr - chunk_addr;
  PERFETTO_DCHECK(payload_off == chunk->unread_payload_off() + frag->hdr_size);

  PERFETTO_DCHECK(chunk->payload_avail >= frag->size_with_header());
  chunk->payload_avail -= frag->size_with_header();
  if (chunk->payload_avail == 0) {
    TRACE_BUFFER_V2_DLOG("  Fully consumed chunk @ %zu", buf_->OffsetOf(chunk));
    if (mode_ == kReadMode) {
      auto& stats = buf_->stats_;
      stats.set_chunks_read(stats.chunks_read() + 1);
      stats.set_bytes_read(stats.bytes_read() + chunk->outer_size());
    }
  }
}

// Tries to reassemble the packet following the chunks in sequence order (by
// cloning the ChunkSeqIterator). If there is a data loss, it consumes anyways
// the fragments. If there isn't enough data, leaves the fragments untouched.
ChunkSeqReader::FragReassemblyResult ChunkSeqReader::ReassembleFragmentedPacket(
    TracePacket* out_packet,
    Frag* initial_frag) {
  PERFETTO_DCHECK(initial_frag->type == Frag::kFragBegin);

  TBChunk* initial_chunk = seq_iter_.chunk();
  if (initial_chunk->flags & kChunkNeedsPatch)
    return FragReassemblyResult::kNotEnoughData;

  struct FragAndChunk {
    FragAndChunk(Frag f, TBChunk* c) : frag(std::move(f)), chunk(c) {}
    Frag frag;
    TBChunk* chunk;
  };

  base::SmallVector<FragAndChunk, 8> frags;
  frags.emplace_back(*initial_frag, initial_chunk);
  ChunkSeqIterator chunk_iter = seq_iter_;  // Make copy.

  // Iterate over chunks using the linked list.
  FragReassemblyResult res;
  for (;;) {
    PERFETTO_DCHECK((chunk_iter.valid()));
    TBChunk* next_chunk = chunk_iter.NextChunkInSequence();
    if (!next_chunk || next_chunk->flags & kChunkNeedsPatch) {
      res = FragReassemblyResult::kNotEnoughData;
      break;
    }
    if (chunk_iter.sequence_gap_detected()) {
      // There is a gap in the sequence ID.
      res = FragReassemblyResult::kDataLoss;
      break;
    }
    FragIterator frag_iter = FragIterator(next_chunk);
    // When we reassemble a fragmented packets we only care about one fragment
    // per chunk. We never need to iterate (more than once) over fragments in a
    // chunk but only look at the first one. Think about it, either:
    // 1. The packet spans across two chunks, so we move to the next chunk,
    //    read the first fragment, and realize that it doesn't continue further.
    // 2. If the packet spans across chunks [0, N], the chunks [1, N-1] must
    //    have only one fragment, with both kFirstPacketContFromPrevChunk
    //    and kLastPacketContOnNextChunk flags.
    std::optional<Frag> frag = frag_iter.NextFragmentInChunk();
    if (!frag.has_value()) {
      if (frag_iter.chunk_corrupted()) {
        res = FragReassemblyResult::kDataLoss;
        break;
      }
      // This can happen if a chunk in the middle of a sequence is empty. Rare
      // but technically possible. See test Fragments_EmptyChunkInTheMiddle.
      continue;
    }

    const auto& frag_type = frag->type;
    if (frag_type == Frag::kFragContinue) {
      frags.emplace_back(*frag, next_chunk);
      continue;
    }
    if (frag_type == Frag::kFragEnd) {
      frags.emplace_back(*frag, next_chunk);

      res = FragReassemblyResult::kSuccess;
      break;
    }
    // else: kFragBegin or kFragWholePacket
    // This is a very weird case: the sequence id is contiguous but somehow the
    // chain of continue-onto-next/prev is broken.
    // In this case we want to leave frags these untouched as they don't belong
    // to us. The next ReadNextPacketInSeqOrder calls will deal with them. Our
    // job here is to consume only fragments for the packet we are trying to
    // reassemble.
    res = FragReassemblyResult::kDataLoss;
    break;
  }  // for (chunk in list)

  for (FragAndChunk& fc : frags) {
    Frag& f = fc.frag;
    if (res == FragReassemblyResult::kSuccess && f.size > 0) {
      if (out_packet)
        out_packet->AddSlice(f.begin, f.size);
    }
    if (res == FragReassemblyResult::kSuccess ||
        res == FragReassemblyResult::kDataLoss) {
      ConsumeFragment(fc.chunk, &f);
    }
  }
  return res;
}

}  // namespace internal

// +---------------------------------------------------------------------------+
// | TraceBuffer                                                               |
// +---------------------------------------------------------------------------+

// static
std::unique_ptr<TraceBufferV2> TraceBufferV2::Create(size_t size_in_bytes,
                                                     OverwritePolicy pol) {
  // The size and alignment of TBChunk have implications on the memory
  // efficiency.
  static_assert(sizeof(TBChunk) == 16);
  static_assert(alignof(TBChunk) == 4);
  std::unique_ptr<TraceBufferV2> trace_buffer(new TraceBufferV2(pol));
  if (!trace_buffer->Initialize(size_in_bytes))
    return nullptr;
  return trace_buffer;
}

TraceBufferV2::TraceBufferV2(OverwritePolicy pol) : overwrite_policy_(pol) {}

bool TraceBufferV2::Initialize(size_t size) {
  size = base::AlignUp(std::max(size, size_t(1)), 4096);
  // The size must be <= 4GB because we use 32 bit offsets everywhere (e.g. in
  // the TBChunk linked list) to reduce memory overhead.
  PERFETTO_CHECK(size <= UINT32_MAX);
  data_ = base::PagedMemory::Allocate(
      size, base::PagedMemory::kMayFail | base::PagedMemory::kDontCommit);
  if (!data_.IsValid()) {
    PERFETTO_ELOG("Trace buffer allocation failed (size: %zu)", size);
    return false;
  }
  size_ = size;
  wr_ = 0;
  used_size_ = 0;
  stats_.set_buffer_size(size);
  return true;
}

void TraceBufferV2::BeginRead() {
  // Start the read at the first chunk after the write cursor. However, if
  // due to out-of-order commits there is another chunk in the same sequence
  // prior to that (even if it's physically after in the buffer) start there
  // to respect sequence FIFO-ness.
  TRACE_BUFFER_V2_DLOG("BeginRead(), wr_=%zu", wr_);
  rd_ = wr_ == used_size_ ? 0 : wr_;
  chunk_seq_reader_.reset();
  ++read_generation_;
}

bool TraceBufferV2::ReadNextTracePacket(
    TracePacket* out_packet,
    PacketSequenceProperties* sequence_properties,
    bool* previous_packet_on_sequence_dropped) {
  *sequence_properties = {0, ClientIdentity(), 0};
  *previous_packet_on_sequence_dropped = false;

  // When reading back chunks, we visit the buffer in two layers
  // (see /docs/design-docs/trace-buffer.md):
  // - The outer layer (this function) iterates in buffer order, starting from
  //   the wr_cursor, as that respects global FIFO-ness.
  // - The inner layer (ChunkSeqReader::ReadNextPacketInSeqOrder()) iterates in
  //   sequence order: it visits all the chunks until the target `chunk` has
  //   been reached.
  for (;;) {
    size_t next_rd = 0;
    // chunk_seq_reader_ is created every time we step on the outer layer, stays
    // alive as long as we are iterating in sequence order, and is destroyed
    // before moving again in buffer order.
    if (!chunk_seq_reader_.has_value()) {
      // Note: in the edge case when the buffer is completely empty, TBChunk
      // is designed in a way such that GetTBChunkAt(0) returns an empty padding
      // chunk with a valid (zero) checksum.
      // This is so we don't need extra branches to deal with this rare case.
      TBChunk* chunk = GetTBChunkAt(rd_);
      if (!chunk->is_padding()) {
        // Starts the inner walk in sequence order.
        chunk_seq_reader_.emplace(this, chunk, ChunkSeqReader::kReadMode);
        continue;
      }
      // If this chunk is empty, try with the next chunk in buffer order.
      next_rd = rd_ + chunk->outer_size();
      // Continues after the else block below...
    } else {
      // ReadNextPacketInSeqOrder() reads only packets up to the current chunk.
      // Eventually it might go back to previous chunks in the sequence (even if
      // they are ahead in buffer order) and might go a bit futher if the last
      // fragment of the chunk is a fragmented packet and continues beyond.
      // But, +- edge cases, it logically reads only up to `chunk`.
      // If it returns false, we should continue in buffer order.
      if (chunk_seq_reader_->ReadNextPacketInSeqOrder(out_packet)) {
        SequenceState& s = *chunk_seq_reader_->seq();
        *sequence_properties = {s.producer_id, s.client_identity, s.writer_id};
        *previous_packet_on_sequence_dropped = s.data_loss;
        s.data_loss = false;
        return true;
      }
      // If ReadNextPacketInSeqOrder rans out of data, skip to the block below
      // which will try with the next chunk in buffer order.
      TBChunk* chunk = chunk_seq_reader_->end();
      next_rd = OffsetOf(chunk) + chunk->outer_size();
    }

    // The buffer-order walk resumes here.
    PERFETTO_DCHECK(next_rd > 0);
    const bool wrap = next_rd >= used_size_;
    chunk_seq_reader_.reset();
    if (next_rd == wr_ || (wrap && wr_ == 0)) {
      // We tried every possible chunk and hit the write cursor. There's no data
      // to read in the buffer.
      return false;
    }
    rd_ = wrap ? 0 : next_rd;
  }  // for(;;)
}

void TraceBufferV2::CopyChunkUntrusted(
    ProducerID producer_id_trusted,
    const ClientIdentity& client_identity_trusted,
    WriterID writer_id,
    ChunkID chunk_id,
    uint16_t num_fragments,
    uint8_t chunk_flags,
    bool chunk_complete,
    const uint8_t* src,
    size_t src_size) {
  TRACE_BUFFER_V2_DLOG("");
  TRACE_BUFFER_V2_DLOG("CopyChunkUntrusted(%zu) @ wr_=%zu", src_size, wr_);
  if (TRACE_BUFFER_V2_VERBOSE_LOGGING())
    DumpForTesting();

  PERFETTO_CHECK(!read_only_);

  if (PERFETTO_UNLIKELY(discard_writes_))
    return DiscardWrite();

  // chunk_complete is true in the majority of cases, and is false only when
  // the service performs SMB scraping (upon flush).
  // If the chunk hasn't been completed, we should only consider the first
  // |num_fragments - 1| packets. For simplicity, we simply disregard
  // the last one when we copy the chunk.
  if (PERFETTO_UNLIKELY(!chunk_complete)) {
    chunk_flags |= kChunkIncomplete;
    if (num_fragments > 0) {
      num_fragments--;
      // These flags should only affect the last packet in the chunk. We clear
      // them, so that TraceBuffer is able to look at the remaining packets in
      // this chunk.
      chunk_flags &= ~kLastPacketContOnNextChunk;
      chunk_flags &= ~kChunkNeedsPatch;
    }
  }

  // Compute the SUM(frags.size).
  size_t all_frags_size = 0;
  internal::FragIterator frag_iter(src, src_size, chunk_flags);
  for (uint16_t frag_idx = 0; frag_idx < num_fragments; frag_idx++) {
    std::optional<Frag> maybe_frag = frag_iter.NextFragmentInChunk();
    if (!maybe_frag.has_value()) {
      // Either we found less fragments than what the header said, or some
      // fragment is out of bounds.
      stats_.set_abi_violations(stats_.abi_violations() + 1);
      PERFETTO_DCHECK(suppress_client_dchecks_for_testing_);
      break;
    }
    Frag& f = *maybe_frag;
    all_frags_size += f.size_with_header();
    TRACE_BUFFER_V2_DLOG("  Frag %u: %p - %p", frag_idx,
                         static_cast<const void*>(f.begin),
                         static_cast<const void*>(f.begin + f.size));
  }
  bool trace_writer_data_drop = frag_iter.trace_writer_data_drop();

  PERFETTO_CHECK(all_frags_size <= src_size);

  // Make space in the buffer for the chunk we are about to copy.

  // If the chunk is incomplete (due to scraping), we want to reserve the
  // whole chunk space in the buffer, to allow later re-commits that will
  // increase the payload size.
  size_t tbchunk_size = chunk_complete ? all_frags_size : src_size;
  const size_t tbchunk_outer_size = TBChunk::OuterSize(tbchunk_size);

  if (PERFETTO_UNLIKELY(tbchunk_outer_size > size_)) {
    // The chunk is bigger than the buffer. Extremely rare, but can happen, e.g.
    // if the user has specified a 16KB buffer and the SMB chunk is 32KB.
    stats_.set_abi_violations(stats_.abi_violations() + 1);
    PERFETTO_DCHECK(suppress_client_dchecks_for_testing_);
    return;
  }

  auto seq_key = MkProducerAndWriterID(producer_id_trusted, writer_id);
  writer_stats_.Insert(seq_key, static_cast<HistValue>(all_frags_size));

  auto [seq_it, seq_is_new] = sequences_.try_emplace(
      seq_key,
      SequenceState(producer_id_trusted, writer_id, client_identity_trusted));
  if (seq_is_new) {
    TRACE_BUFFER_V2_DLOG("  Added seq %x", seq_key);
  }

  SequenceState& seq = seq_it->second;
  if (trace_writer_data_drop) {
    stats_.set_trace_writer_packet_loss(stats_.trace_writer_packet_loss() + 1);
    seq.data_loss = true;
  }

  // Don't allow re-commit of chunks that have been consumed already. It's too
  // late and they will only mess up future chunks more.
  if (seq.last_chunk_id_consumed.has_value() &&
      ChunkIdCompare(chunk_id, *seq.last_chunk_id_consumed) <= 0) {
    stats_.set_chunks_discarded(stats_.chunks_discarded() + 1);
    PERFETTO_DCHECK(suppress_client_dchecks_for_testing_);
    return;
  }

  // If there isn't enough room from the given write position: write a padding
  // record to clear the end of the buffer, wrap and start at offset 0.
  const size_t cached_size_to_end = size_to_end();
  if (PERFETTO_UNLIKELY(tbchunk_outer_size > cached_size_to_end)) {
    // If we reached the end of the buffer and we are using discard policy,
    // this is where we stop. This buffer will no longer accept data.
    if (overwrite_policy_ == kDiscard)
      return DiscardWrite();

    DeleteNextChunksFor(cached_size_to_end);
    wr_ = 0;
    stats_.set_write_wrap_count(stats_.write_wrap_count() + 1);
    PERFETTO_DCHECK(size_to_end() >= tbchunk_outer_size);
  }

  // Deletes all chunks from |wptr_| to |wptr_| + |record_size|.
  DeleteNextChunksFor(tbchunk_outer_size);

  // Find the insert position in the SequenceState's chunk list. We iterate the
  // list in reverse order as in the majority of cases chunks arrive naturally
  // in order. SMB scraping is really the only thing that might commit chunks
  // slightly out of order.
  auto& chunk_list = seq.chunks;
  auto insert_pos = chunk_list.rbegin();
  TBChunk* recommit_chunk = nullptr;
  for (; insert_pos != chunk_list.rend(); ++insert_pos) {
    TBChunk* other_chunk = GetTBChunkAt(*insert_pos);
    int cmp = ChunkIdCompare(chunk_id, other_chunk->chunk_id);
    if (cmp > 0)
      break;
    if (cmp == 0) {
      // The producer is trying to re-commit a previously copied chunk. This
      // can happen when the service does SMB scraping (the same chunk could
      // be scraped more than once), and later the producer does a commit.
      // We allow recommit only if the new chunk is larger than the existing.
      recommit_chunk = other_chunk;
      break;
    }
  }

  const uint16_t all_frags_size_u16 = static_cast<uint16_t>(all_frags_size);

  // In the case of a re-commit we don't need to create a new chunk, we just
  // want to overwrite the existing one.
  if (PERFETTO_UNLIKELY(recommit_chunk)) {
    const uint8_t recommit_flags = recommit_chunk->flags & kFlagsMask;
    if (all_frags_size < recommit_chunk->payload_size ||
        all_frags_size > recommit_chunk->size ||
        (recommit_flags & chunk_flags) != recommit_flags) {
      // The payload should never shrink, cannot grow more than the original
      // chunk size. Flags can be added but not removed.
      stats_.set_abi_violations(stats_.abi_violations() + 1);
      PERFETTO_DCHECK(suppress_client_dchecks_for_testing_);
      return;
    }
    recommit_chunk->flags &= ~kChunkIncomplete;
    if (all_frags_size == recommit_chunk->payload_size) {
      TRACE_BUFFER_V2_DLOG("  skipping recommit of identical chunk");
      return;
    }
    uint16_t payload_consumed =
        recommit_chunk->payload_size - recommit_chunk->payload_avail;
    recommit_chunk->payload_size = all_frags_size_u16;
    recommit_chunk->payload_avail = all_frags_size_u16 - payload_consumed;
    memcpy(recommit_chunk->fragments_begin(), src, all_frags_size);
    recommit_chunk->flags |= chunk_flags;
    stats_.set_chunks_rewritten(stats_.chunks_rewritten() + 1);
    return;
  }

  TBChunk* tbchunk = CreateTBChunk(wr_, tbchunk_size);
  tbchunk->payload_size = all_frags_size_u16;
  tbchunk->payload_avail = all_frags_size_u16;
  tbchunk->chunk_id = chunk_id;
  tbchunk->flags = chunk_flags;
  tbchunk->pri_wri_id = seq_key;
  auto* payload_begin = reinterpret_cast<uint8_t*>(tbchunk) + sizeof(TBChunk);
  uint8_t* wptr = payload_begin;

  // Copy all the (valid) fragments from the SMB chunk to the TBChunk.
  memcpy(wptr, src, all_frags_size);

  PERFETTO_DCHECK(wr_ == OffsetOf(tbchunk));
  if (insert_pos != chunk_list.rbegin()) {
    stats_.set_chunks_committed_out_of_order(
        stats_.chunks_committed_out_of_order() + 1);
  }
  chunk_list.InsertAfter(insert_pos, wr_);
  if (chunk_list.size() == 1 && !seq_is_new) {
    PERFETTO_DCHECK(empty_sequences_ > 0);
    --empty_sequences_;
  }

  TRACE_BUFFER_V2_DLOG(" END OF CopyChunkUntrusted(%zu) @ wr=%zu", src_size,
                       wr_);

  wr_ += tbchunk_outer_size;
  PERFETTO_DCHECK(wr_ <= size_ && wr_ <= used_size_);
  wr_ = wr_ >= size_ ? 0 : wr_;

  stats_.set_chunks_written(stats_.chunks_written() + 1);
  stats_.set_bytes_written(stats_.bytes_written() + tbchunk_outer_size);

  // We purge SequenceStates(s) only here, because other parts of the readback
  // code (BeginRead()/ReadNextTracePacket()) need to cache SequenceState*
  // pointers via seq_iter_ acros invocations.
  if (empty_sequences_ > kEmptySequencesGcTreshold)
    DeleteStaleEmptySequences();
}

TraceBufferV2::TBChunk* TraceBufferV2::CreateTBChunk(size_t off, size_t size) {
  DcheckIsAlignedAndWithinBounds(off);
  size_t end = off + TBChunk::OuterSize(size);
  if (PERFETTO_UNLIKELY(end > used_size_)) {
    used_size_ = end;
    data_.EnsureCommitted(end);
  }
  TBChunk* chunk = GetTBChunkAtUnchecked(off);
  return new (chunk) TBChunk(off, size);
}

// Deletes (by marking the record invalid and removing form the index) all
// chunks from wr_ to (wr_ + bytes_to_clear).
// Graphically, assume the initial situation is the following (|wptr_| = 10).
// |0        |10 (wptr_)       |30       |40                 |60
// +---------+-----------------+---------+-------------------+---------+
// | Chunk 1 | Chunk 2         | Chunk 3 | Chunk 4           | Chunk 5 |
// +---------+-----------------+---------+-------------------+---------+
//           |_________Deletion range_______|~~padding chunk~~|
//
// A call to DeleteNextChunksFor(32) will remove chunks 2,3,4 and create a
// 18 bytes (60 - 42) padding chunk, between the end of the deletion range and
// the beginning of Chunk 5.
// Unlike the old v1 impl, here DeleteNextChunksFor also takes care of writing
// the padding chunk in case of truncation.
void TraceBufferV2::DeleteNextChunksFor(size_t bytes_to_clear) {
  TRACE_BUFFER_V2_DLOG("DeleteNextChunksFor(%zu) @ wr=%zu", bytes_to_clear,
                       wr_);
  PERFETTO_CHECK(!discard_writes_);
  PERFETTO_DCHECK(bytes_to_clear >= sizeof(TBChunk));
  PERFETTO_DCHECK((bytes_to_clear % TBChunk::alignment()) == 0);

  DcheckIsAlignedAndWithinBounds(wr_);
  const size_t clear_end = wr_ + bytes_to_clear;
  PERFETTO_DCHECK(clear_end <= size_);

  // This loop erases all the existing chunks in the clear range.
  for (size_t off = wr_, next_off = 0; off < clear_end; off = next_off) {
    if (PERFETTO_UNLIKELY(off >= used_size_)) {
      // This happens only on the first round of writing, when there are no
      // chunks in the buffer yet. There is nothing to delete here, easy.
      break;
    }
    TBChunk* chunk = GetTBChunkAt(off);
    const auto chunk_outer_size = chunk->outer_size();
    next_off = off + chunk_outer_size;
    if (chunk->is_padding()) {
      stats_.set_padding_bytes_cleared(stats_.padding_bytes_cleared() +
                                       chunk_outer_size);
      continue;
    }
    // Reads all the packets up to the current chunk:
    // If there are chunks prior to this (due to OOO) reads first those
    // If the last fragment continues in the next chunk, reads that as well
    // (but then stops and doesn't read fully the subsequent chunk).
    ChunkSeqReader csr(this, chunk, ChunkSeqReader::kEraseMode);
    bool has_cleared_unconsumed_fragments = false;
    for (;;) {
      // If we have ProtoVMs, we need to readout each packet and pass to
      // protovm. If not, we still need to do reads, but without having to
      // accumulated data in a packet.
      TracePacket* maybe_packet = nullptr;
      if (!protovms_.empty()) {
        overwritten_packet_.Clear();
        maybe_packet = &overwritten_packet_;
      }
      if (!csr.ReadNextPacketInSeqOrder(maybe_packet)) {
        break;
      }
      if (maybe_packet) {
        MaybeProcessOverwrittenPacketWithProtoVm(*maybe_packet,
                                                 csr.seq()->producer_id);
      }
      has_cleared_unconsumed_fragments = true;
    }

    // In future this branch should become "&& !protovm_has_consumed_packet"
    // We shouldn't report a data loss if ProtoVM merged the outgoing packet.
    if (has_cleared_unconsumed_fragments) {
      csr.seq()->data_loss = true;
    }

    // ChunkSeqReader(kEraseMode) must delete the chunk once
    // ReadNextPacketInSeqOrder() returns false.
    PERFETTO_DCHECK(chunk->is_padding());

    stats_.set_chunks_overwritten(stats_.chunks_overwritten() + 1);
    stats_.set_bytes_overwritten(stats_.bytes_overwritten() + chunk_outer_size);
  }

  // Having consumed the packets above, this loop wipes out the contents of the
  // chunks in a second pass.
  for (size_t off = wr_; off < clear_end && off < used_size_;) {
    TBChunk* chunk = GetTBChunkAt(off);
    PERFETTO_DCHECK(chunk->is_padding());
    size_t chunk_end = off + chunk->outer_size();
    if (clear_end > off && clear_end < chunk_end) {
      PERFETTO_DCHECK(chunk_end - clear_end >= sizeof(TBChunk));
      // Create a zero padding chunk at the end.
      TBChunk* pad_chunk =
          CreateTBChunk(clear_end, chunk_end - clear_end - sizeof(TBChunk));
      stats_.set_padding_bytes_written(stats_.padding_bytes_written() +
                                       pad_chunk->outer_size());
    }
    off += chunk->outer_size();
  }
}

bool TraceBufferV2::TryPatchChunkContents(ProducerID producer_id,
                                          WriterID writer_id,
                                          ChunkID chunk_id,
                                          const Patch* patches,
                                          size_t patches_size,
                                          bool other_patches_pending) {
  PERFETTO_CHECK(!read_only_);

  ProducerAndWriterID seq_key = MkProducerAndWriterID(producer_id, writer_id);
  auto seq_it = sequences_.find(seq_key);
  if (seq_it == sequences_.end()) {
    stats_.set_patches_failed(stats_.patches_failed() + 1);
    return false;
  }

  // We have to do a linear search to find the chunk to patch. In the majority
  // of cases the chunk to patch is one of the last ones committed, so we walk
  // the list backwards.
  auto& chunk_list = seq_it->second.chunks;
  TBChunk* chunk = nullptr;
  for (auto it = chunk_list.rbegin(); it != chunk_list.rend(); ++it) {
    TBChunk* it_chunk = GetTBChunkAt(*it);
    if (it_chunk->chunk_id == chunk_id) {
      chunk = it_chunk;
      break;
    }
  }

  if (chunk == nullptr) {
    stats_.set_patches_failed(stats_.patches_failed() + 1);
    return false;
  }

  size_t payload_size = chunk->payload_size;

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
    PERFETTO_DCHECK(offset_untrusted >= payload_size - chunk->payload_avail);
    TRACE_BUFFER_V2_DLOG("PatchChunk {%" PRIu32 ",%" PRIu32
                         ",%u} size=%zu @ %zu with {%02x %02x %02x %02x}",
                         producer_id, writer_id, chunk_id,
                         size_t(chunk->payload_size), offset_untrusted,
                         patches[i].data[0], patches[i].data[1],
                         patches[i].data[2], patches[i].data[3]);
    uint8_t* dst = chunk->fragments_begin() + offset_untrusted;
    memcpy(dst, &patches[i].data[0], Patch::kSize);
  }
  TRACE_BUFFER_V2_DLOG(
      "Chunk raw (after patch): %s",
      base::HexDump(chunk->fragments_begin(), chunk->payload_size).c_str());
  stats_.set_patches_succeeded(stats_.patches_succeeded() + patches_size);
  if (!other_patches_pending) {
    chunk->flags &= ~kChunkNeedsPatch;
  }
  return true;
}

void TraceBufferV2::DiscardWrite() {
  PERFETTO_DCHECK(overwrite_policy_ == kDiscard);
  discard_writes_ = true;
  stats_.set_chunks_discarded(stats_.chunks_discarded() + 1);
  TRACE_BUFFER_V2_DLOG("  discarding write");
}

// When a sequence has 0 chunks we could delete it straight away. However doing
// do would remove also the last_chunk_id_consumed used to detect data losses.
// Here we have to balance the tradeoff between:
// - Bounding memory usage: if we have a lot of writer threads, we can't just
//   keeping growing the SequenceStates without bounds.
// - Retaining our ability to detect data losses for sporadic writers.
// Here the decision is to keep the last 1024 (kKeepLastEmptySeq) empty
// sequences and delete anything older. We have some hysteresis and start the GC
// process only after hitting 1024+128 (kEmptySequencesGcTreshold) empty seqs.
// This is to avoid thrashing with a sort every time one sequence becomes empty.
// Also we have to defer the deletion of SequenceState outside of
// ReadNextTracePacket() calls, as that caches SequenceState* pointers in
// seq_iter_. This is called only at the end of each CopyChunkUntrusted().
void TraceBufferV2::DeleteStaleEmptySequences() {
  // Build a vector of iterators; sort it; delete the first size() - kThreshold.
  using SeqIterator = decltype(sequences_)::iterator;
  std::vector<SeqIterator> empty_seqs;
  empty_seqs.reserve(sequences_.size());
  for (auto it = sequences_.begin(); it != sequences_.end(); ++it) {
    if (it->second.chunks.empty())
      empty_seqs.emplace_back(it);
  }
  static_assert(kEmptySequencesGcTreshold >= kKeepLastEmptySeq);
  if (empty_seqs.size() < kEmptySequencesGcTreshold)
    return;

  std::sort(empty_seqs.begin(), empty_seqs.end(),
            [](const SeqIterator& a, const SeqIterator& b) {
              return a->second.age_for_gc < b->second.age_for_gc;
            });

  size_t n_oldest = empty_seqs.size() - kKeepLastEmptySeq;
  for (size_t i = 0; i < n_oldest; ++i) {
    sequences_.erase(empty_seqs[i]);
  }
  empty_sequences_ = kKeepLastEmptySeq;

  // This is not really required in the current implementation and is here just
  // to spot bugs while refactoring the code in future. This function is
  // only called by CopyChunkUntrusted(), but the chunk_seq_reader_ (which can
  // hold onto a SequenceState* pointer) is reset by every BeginRead() call.
  // By design BeginRead()/ReadNextTracePacket() cannot overlap with CCU().
  chunk_seq_reader_.reset();
}

std::unique_ptr<TraceBuffer> TraceBufferV2::CloneReadOnly() const {
  std::unique_ptr<TraceBufferV2> buf(new TraceBufferV2(CloneCtor(), *this));
  if (!buf->data_.IsValid())
    return nullptr;  // PagedMemory::Allocate() failed. We are out of memory.
  return buf;
}

size_t TraceBufferV2::GetMemoryUsageBytes() const {
  size_t total_bytes = size();
  for (const Vm& vm : protovms_) {
    total_bytes += vm.instance->GetMemoryUsageBytes();
  }
  return total_bytes;
}

TraceBufferV2::TraceBufferV2(CloneCtor, const TraceBufferV2& src)
    : overwrite_policy_(src.overwrite_policy_),
      read_generation_(src.read_generation_),
      read_only_(true),
      discard_writes_(src.discard_writes_) {
  if (!Initialize(src.data_.size()))
    return;  // TraceBufferV2::Clone() will check |data_| and return nullptr.

  // The assignments below must be done after Initialize().

  data_.EnsureCommitted(src.used_size_);
  memcpy(data_.Get(), src.data_.Get(), src.used_size_);
  used_size_ = src.used_size_;
  wr_ = src.wr_;

  stats_ = src.stats_;
  stats_.set_bytes_read(0);
  stats_.set_chunks_read(0);
  stats_.set_readaheads_failed(0);
  stats_.set_readaheads_succeeded(0);

  // Finally copy over the SequenceState map.
  sequences_ = src.sequences_;

  for (const auto& vm : src.protovms_) {
    auto vm_cloned = vm.CloneReadOnly();
    if (!vm_cloned.instance) {
      PERFETTO_ELOG("Failed to clone ProtoVMs");
      protovms_.clear();
      break;
    }
    protovms_.push_back(std::move(vm_cloned));
  }
}

TraceBufferV2::Vm TraceBufferV2::Vm::CloneReadOnly() const {
  Vm cloned_vm;
  cloned_vm.data_source_name = data_source_name;
  cloned_vm.program_hash = program_hash;
  cloned_vm.memory_limit_kb = memory_limit_kb;
  cloned_vm.producers = producers;
  cloned_vm.instance = instance->CloneReadOnly();
  return cloned_vm;
}

void TraceBufferV2::DumpForTesting() {
  PERFETTO_DLOG(
      "------------------- DUMP BEGIN ------------------------------");
  PERFETTO_DLOG("wr=%zu, size=%zu, used_size=%zu", wr_, size_, used_size_);
  if (chunk_seq_reader_.has_value()) {
    PERFETTO_DLOG("rd=%zu, target=%zu", OffsetOf(chunk_seq_reader_->iter()),
                  OffsetOf(chunk_seq_reader_->end()));
  } else {
    PERFETTO_DLOG("rd=invalid");
  }
  for (size_t rd = 0; rd < size_;) {
    TBChunk* c = GetTBChunkAtUnchecked(rd);
    bool checksum_valid = c->IsChecksumValid(rd);
    if (checksum_valid) {
      PERFETTO_DLOG(
          "[%06zu-%06zu] size=%05u(%05u) id=%05u pr_wr=%08x flags=%08x", rd,
          rd + c->outer_size(), c->payload_size,
          c->payload_size - c->payload_avail, c->chunk_id, c->pri_wri_id,
          c->flags);
      rd += c->outer_size();
      continue;
    }
    size_t zero_start = rd;
    // Count zeros.
    for (; rd < size_ && begin()[rd] == 0; rd++) {
    }
    PERFETTO_DLOG("%zu zeros, %zu left", rd - zero_start, size_ - rd);
    break;
  }
  PERFETTO_DLOG("------------------------------------------------------------");
}

TraceBufferV2::Vm::Vm() = default;
TraceBufferV2::Vm::~Vm() = default;
TraceBufferV2::Vm::Vm(Vm&&) noexcept = default;

void TraceBufferV2::MaybeSetUpProtoVm(const std::string& data_source_name,
                                      const std::string& program_bytes,
                                      uint32_t memory_limit_kb,
                                      ProducerID producer_id) {
  Vm* vm = nullptr;
  // Re-use existing ProtoVM instance, if any.
  uint64_t program_hash = base::MurmurHashValue(program_bytes);
  auto vm_it =
      std::find_if(protovms_.begin(), protovms_.end(), [&](const Vm& vm) {
        return vm.data_source_name == data_source_name &&
               vm.program_hash == program_hash;
      });
  if (vm_it != protovms_.end()) {
    vm = &(*vm_it);
  } else {
    // Otherwise instantiate new ProtoVM
    Vm new_vm;
    new_vm.data_source_name = data_source_name;
    new_vm.program_hash = program_hash;
    new_vm.memory_limit_kb = memory_limit_kb;

    protozero::ConstBytes program_bytes_view{
        reinterpret_cast<const uint8_t*>(program_bytes.data()),
        program_bytes.size()};

    new_vm.instance = std::make_unique<protovm::Vm>(
        program_bytes_view, new_vm.memory_limit_kb * 1024);
    if (!new_vm.instance) {
      PERFETTO_ELOG("Failed to allocate ProtoVM");
      return;
    }

    protovms_.push_back(std::move(new_vm));
    vm = &protovms_.back();
  }
  // Update ProtoVM's producer IDs
  vm->producers.insert(producer_id);
}

void TraceBufferV2::MaybeProcessOverwrittenPacketWithProtoVm(
    const TracePacket& packet,
    ProducerID producer) {
  protovm_patch_.clear();

  for (auto& vm : protovms_) {
    if (vm.producers.find(producer) == vm.producers.end()) {
      continue;
    }
    // TODO(keanmariotti): add an optimized path for the case
    // "packet.slices().size() == 1" (zero copy)
    if (protovm_patch_.empty()) {
      packet.GetRawBytes(&protovm_patch_);
    }
    protozero::ConstBytes bytes{
        reinterpret_cast<const uint8_t*>(protovm_patch_.data()),
        protovm_patch_.size()};
    auto status = vm.instance->ApplyPatch(bytes);
    if (status.IsOk()) {
      break;
    }
    if (status.IsAbort()) {
      // TODO(keanmariotti): consider doing something more here. Ideally
      // triggering a field upload containing the stacktrace.
      PERFETTO_ELOG("ProtoVM abort while applying patch. Stacktrace:\n%s",
                    base::Join(status.stacktrace(), "\n").c_str());
    }
  }
}

}  // namespace perfetto
