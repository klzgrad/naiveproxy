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

#ifndef SRC_TRACING_SERVICE_TRACE_BUFFER_V2_H_
#define SRC_TRACING_SERVICE_TRACE_BUFFER_V2_H_

#include <stdint.h>
#include <string.h>

#include <limits>
#include <optional>
#include <unordered_map>

#include "perfetto/base/flat_set.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/circular_queue.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/paged_memory.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/thread_annotations.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/core/client_identity.h"
#include "perfetto/ext/tracing/core/slice.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/ext/tracing/core/trace_stats.h"
#include "src/tracing/service/histogram.h"
#include "src/tracing/service/trace_buffer.h"

namespace perfetto {

class TracePacket;
class TraceBufferV2;

namespace protovm {
class Vm;
}

namespace internal {

// See /docs/design-docs/trace-buffer.md for details about the design and
// implementation details of TraceBuffer.

// +---------------------------------------------------------------------------+
// | TBChunk                                                                   |
// +---------------------------------------------------------------------------+
// TBChunk is the struct, stored in the trace buffer memory as a result of
// calling CopyChunkUntrusted from a SMB chunk.
// TBChunk exists only in the TraceBuffer PagedMemory `data_`, never on the
// stack or on the heap. It is followed by the fragments and alignment padding.
// A TBChunk is very similar to a SMB chunk with the following caveats:
// - The sizeof() both is the same (16 bytes). This is very important to keep
//   patches offsets consistent.
// - The SMB chunk maintains a counter of fragments. TBChunk instead does
//   byte-based bookkeeping, as that reduces the complexity of the iterators.
// - The layout of the fields is slightly different, but they both contains
//   ProducerID, WriterID, ChunkID, fragment counts/sizes and flags.
//   The SMB chunk layout is an ABI. The TCHunk layout is not: it is an
//   implementation detail and can be changed.
// - TBChunk maintains a basic checksum for each chunk (only for debug builds).
struct TBChunk {
  static constexpr size_t kMaxSize = std::numeric_limits<uint16_t>::max();
  static uint8_t Checksum(size_t off, size_t size) {
    // Note: the checksum must be 0 for (off=0,size=0). See the comment in
    // ReadNextTracePacket() about the edge case of the buffer completely empty.
    return ((off >> 24) ^ (off >> 16) ^ (off >> 8) ^ off ^ (size >> 8) ^ size) &
           0xFF;
  }

  explicit TBChunk(size_t off, size_t size_)
      : size(static_cast<uint16_t>(size_)), checksum(Checksum(off, size)) {
    PERFETTO_DCHECK(size_ <= kMaxSize);
  }

  // The ChunkID, as specified by the TraceWriter in the original SMB chunk.
  ChunkID chunk_id = 0;

  // A combination of producer and writer ID. This forms the primary key to
  // look up the corresponding SequenceState from TraceBuffer.sequences_.
  ProducerAndWriterID pri_wri_id = 0;

  // Size of the chunk, excluding the TBCHunk header itself, and without
  // accounting for any alignment. This doesn't change throughout the lifecycle
  // of a chunk.
  uint16_t size = 0;

  // The size of the valid fragments payload. This is typically == size, with
  // the exception of incomplete chunks committed while scraping.
  // The payload of incomplete chunks can increase (up to the original chunk
  // size). Wheh we scrape we set size = SMB chunk size, and
  // payload_size = all_frag_size.
  uint16_t payload_size = 0;

  // The number of payload bytes unconsumed. This starts at payload_size and
  // shrinks until it reaches 0 as we consume fragments.
  // It is always <= size and <= payload_size.
  // Effectively (payload_size - payload-avail) is the offset of the the next
  // unconsumed fragment header (the varint with the size).
  uint16_t payload_avail = 0;

  // These are == the SharedMemoryABI's chunk flags, with the addition of
  // MSB flags added by TraceBufferV2 like kChunkIncomplete (0x80) which doesn't
  // exist at the ABI level, but are synthesized here.
  uint8_t flags = 0;

  // This is used for (D)CHECKS to verify the integrity of the chunk.
  // This is a hash of the offset in the buffer and the size.
  uint8_t checksum = 0;

  // Returns the offset to the next unread fragment in the chunk. Note that this
  // points to the next fragment header (the varint with the size) NOT payload.
  uint16_t unread_payload_off() {
    PERFETTO_DCHECK((payload_avail <= payload_size));
    return payload_size - payload_avail;
  }

  // TODO(primiano): in theory we could align just up to alignof(TBChunk)
  // rather than sizeof(TBChunk), which is 4 rather than 16 bytes. That would
  // reduce internal fragmentation.
  // However, doing so requires some careful thinking as that creates the
  // opportunity for more interesting edge cases, where overwriting a chunk
  // leaves less than sizeof(TBChunk), leaving no space for a padding header
  // after it. See TraceBufferV2Test.Overwrite_SizeDiffLessThanChunkHeader.
  static inline constexpr size_t alignment() { return sizeof(TBChunk); }

  static inline size_t OuterSize(size_t sz) {
    return base::AlignUp(sizeof(TBChunk) + sz, alignment());
  }
  size_t outer_size() { return OuterSize(size); }

  bool is_padding() const { return pri_wri_id == 0; }

  uint8_t* fragments_begin() {
    return reinterpret_cast<uint8_t*>(this) + sizeof(TBChunk);
  }

  uint8_t* fragments_end() { return fragments_begin() + payload_size; }

  bool IsChecksumValid(size_t off) { return Checksum(off, size) == checksum; }
};

// +---------------------------------------------------------------------------+
// | SequenceState                                                             |
// +---------------------------------------------------------------------------+
// Holds the state for each sequence that has TBCHunk(s) in the buffer.
// Remember that this struct must be copyable for CloneReadOnly(). Don't hold
// onto any pointers in here.
// SequenceState(s) are not deleted aggressively to preserve the
// last_chunk_id_consumed and detect data losses in long tracing mode. We allow
// the last kKeepLastEmptySeq to stay alive to balance data loss detection with
// memory bloats.
struct SequenceState {
  SequenceState(ProducerID, WriterID, ClientIdentity);
  ~SequenceState();
  SequenceState(const SequenceState&) noexcept;
  SequenceState& operator=(const SequenceState&) noexcept;

  ProducerID producer_id = 0;
  WriterID writer_id = 0;
  ClientIdentity client_identity{};

  // This is semantically a boolean that resets every time BeginRead()
  // increments the generation counter. The semantic is:
  // skip := skip_in_generation == TraceBuffer.read_generation_.
  uint64_t skip_in_generation = 0;

  // Used by DeleteStaleEmptySequences() to keep the latest kKeepLastEmptySeq
  // objects around.
  uint64_t age_for_gc = 0;

  std::optional<ChunkID> last_chunk_id_consumed;

  // This is set whenever a data loss is detected and cleared when reading the
  // next packet for the sequence (which will report previous_packet_dropped).
  bool data_loss = false;

  // An ordered list of chunk offsets, sorted by their ChunkID. Each member
  // corresponsds to the offset within buf_ for the chunk.
  // We store buffer offsets rather than pointers to make buffer cloning easier.
  // This is effectively a deque of TBChunk* (% a call to GetTBChunkAt(off)).
  base::CircularQueue<size_t> chunks;
};

// +---------------------------------------------------------------------------+
// | FragIterator                                                              |
// +---------------------------------------------------------------------------+
// A tokenized fragment in the buffer. Holds the fragment size & boundaries.
// This struct is returned by FragIterator when tokenizing the fragments in
// CopyChunkUntrusted() or reading the buffer.
// Frag instances are short lived.
struct Frag {
  enum FragType : uint8_t {
    // 1 packet == 1 fragment.
    kFragWholePacket,

    // Fragmentation cases:

    // The last fragment of a chunk, when kLastPacketContinuesOnNextChunk
    kFragBegin,

    // The only fragment of a chunk when both kLastPacketContinuesOnNextChunk &
    // kFirstPacketContinuesFromPrevChunk
    kFragContinue,

    // The first fragment of a chunk when kFirstPacketContinuesFromPrevChunk
    kFragEnd,
  };

  // Diagram for the member variables below.
  //  +- `begin`
  //  |
  //  [VarInt header][ Fragment payload ]
  //  (  hdr_size   )(       size       )
  //  (         size_with_header        )

  // Pointes to the fragment payload, immediately after the header.
  const uint8_t* const begin = nullptr;
  FragType const type = kFragWholePacket;
  uint8_t const hdr_size = 0;  // Size of the varint that tells the frag size.
  uint16_t const size = 0;     // Size of the payload (the varint value).

  uint16_t size_with_header() { return size + hdr_size; }

  Frag(const uint8_t* b, FragType t, uint8_t h, uint16_t s)
      : begin(b), type(t), hdr_size(h), size(s) {}
};

// A simple class that tokenizes fragments in a chunk and allows forward-only
// iteration.
// It deals with untrusted data, detecting malformed / out of bounds scenarios.
// It does not alter the state of the buffer.
// This is used in three places:
// - In CopyChunkUntrusted(): to tokenize the fragments and figure out the
//   "effective" size of the chunk, to get rid of chunk padding.
// - In ChunkSeqReader::ReadNextPacket(): for the main read logic.
// - In ReassembleFragmentedPacket(): for the fragment reassembly logic.
class FragIterator {
 public:
  explicit FragIterator(TBChunk* chunk)
      : chunk_begin_(chunk->fragments_begin()),
        chunk_size_(chunk->payload_size),
        next_frag_off_(chunk->unread_payload_off()),
        chunk_flags_(chunk->flags) {}

  FragIterator(const uint8_t* begin, size_t size, uint8_t flags)
      : chunk_begin_(begin), chunk_size_(size), chunk_flags_(flags) {}

  std::optional<Frag> NextFragmentInChunk();
  size_t next_frag_off() const { return next_frag_off_; }
  bool chunk_corrupted() { return chunk_corrupted_; }
  bool trace_writer_data_drop() { return trace_writer_data_drop_; }

 private:
  const uint8_t* chunk_begin_ = nullptr;
  size_t chunk_size_ = 0;
  size_t next_frag_off_ = 0;
  uint8_t chunk_flags_ = 0;
  bool chunk_corrupted_ = false;
  bool trace_writer_data_drop_ = false;
};

// +---------------------------------------------------------------------------+
// | ChunkSeqIterator                                                          |
// +---------------------------------------------------------------------------+
// A simple utility class that iterates over the ordered list of TBChunk for
// a given SequenceState. It merely follows the SequenceState.chunks queue
// and detects gaps.
class ChunkSeqIterator {
 public:
  // Rewinds to the first chunk of the sequence.
  explicit ChunkSeqIterator(TraceBufferV2*, SequenceState*);
  ChunkSeqIterator() = default;  // Creates an invalid object, for default init.
  ChunkSeqIterator(const ChunkSeqIterator&) = default;  // Allow copy.
  ChunkSeqIterator& operator=(const ChunkSeqIterator&) = default;

  TBChunk* NextChunkInSequence();
  void EraseCurrentChunk();
  TBChunk* chunk() const { return chunk_; }
  bool sequence_gap_detected() const { return sequence_gap_detected_; }
  bool valid() const { return !!seq_ && !!chunk_; }

 private:
  TraceBufferV2* buf_ = nullptr;
  SequenceState* seq_ = nullptr;
  TBChunk* chunk_ = nullptr;
  bool sequence_gap_detected_ = false;
  size_t list_idx_ = 0;  // Offset of the current chunk in seq_.chunks.
};

// +---------------------------------------------------------------------------+
// | ChunkSeqReader                                                            |
// +---------------------------------------------------------------------------+
// Encapsulates most of the readback complexity. It reads and consumes chunks
// in sequence order, as follows:
//
// When constructed, the caller must pass a target TBChunk as argument. This
// is the chunk where we will stop the iteration *.
// At readback time this is the next chunk in the buffer that we want to read.
// At overwrite time this is the chunk that we are about to overwriter.
// In both cases, because of OOO commits, the next chunk in buffer-order might
// not necessarily be the next chunk that should be consumed in FIFO order
// (although in the vast majority cases we expect them to be in order).
// Upon construction, it rewinds all the way back in the `SequenceState.chunks`
// (using `ChunkSeqIterator`) and starts the iteration from there.
// It keeps reading packets until we reach the target TBChunk passed in the
// constructor.
// In some cases (fragmentation) it might read beyon the target chunk. This is
// to reassembly a packet that started in the target chunk and continued later
// on.
// When doing so it just consumes the fragment required for reassembly and
// leaves the other packets in the chunk untouched, to preserve global FIFOness.
class ChunkSeqReader {
 public:
  enum Mode {
    kReadMode,   // For standard readback.
    kEraseMode,  // For read-while-overwriting in DeleteNextChunksFor().
  };

  ChunkSeqReader(TraceBufferV2*, TBChunk*, Mode);

  bool ReadNextPacketInSeqOrder(TracePacket*);
  TBChunk* end() { return end_; }
  TBChunk* iter() { return iter_; }
  SequenceState* seq() { return seq_; }

 private:
  ChunkSeqReader(const ChunkSeqReader&) = delete;
  ChunkSeqReader& operator=(const ChunkSeqReader&) = delete;
  ChunkSeqReader(ChunkSeqReader&&) = delete;
  ChunkSeqReader& operator=(ChunkSeqReader&&) = delete;

  enum class FragReassemblyResult { kSuccess = 0, kNotEnoughData, kDataLoss };
  FragReassemblyResult ReassembleFragmentedPacket(TracePacket* out_packet,
                                                  Frag* initial_frag);
  void ConsumeFragment(TBChunk*, Frag*);

  TraceBufferV2* const buf_ = nullptr;
  Mode const mode_;

  // This is the chunk passed in the constructor and is our stopping point.
  // It never changes throughout the lifetime of ChunkSeqReader.
  // Note that this is NOT the end of the sequence. This is simply where we
  // want to stop iterating, which might be < seq_.end().
  TBChunk* const end_ = nullptr;

  SequenceState* const seq_ = nullptr;

  ChunkSeqIterator seq_iter_;

  // This is initially reset to the first chunk of the sequence, and advanced
  // until we hit end_. chunk_ and end_ always belong to the same seq_.
  TBChunk* iter_ = nullptr;

  FragIterator frag_iter_;
};

}  // namespace internal

// +---------------------------------------------------------------------------+
// | TraceBufferV2                                                             |
// +---------------------------------------------------------------------------+
class TraceBufferV2 : public TraceBuffer {
 public:
  using TBChunk = internal::TBChunk;
  using OverwritePolicy = TraceBuffer::OverwritePolicy;
  using Patch = TraceBuffer::Patch;
  using PacketSequenceProperties = TraceBuffer::PacketSequenceProperties;

  // Represents a ProtoVM instance and some metadata
  struct Vm {
    Vm();
    ~Vm();
    Vm(Vm&&) noexcept;
    Vm CloneReadOnly() const;

    std::unique_ptr<protovm::Vm> instance;
    std::string data_source_name;
    uint64_t program_hash = 0;
    uint32_t memory_limit_kb = 0;
    base::FlatSet<ProducerID> producers;
  };

  // Can return nullptr if the memory allocation fails.
  static std::unique_ptr<TraceBufferV2> Create(size_t size_in_bytes,
                                               OverwritePolicy = kOverwrite);

  // Copies a Chunk from a producer Shared Memory Buffer into the trace buffer.
  // |src| points to the first packet in the SharedMemoryABI's chunk shared with
  // an untrusted producer. "untrusted" here means: the producer might be
  // malicious and might change |src| concurrently while we read it (internally
  // this method memcpy()-s first the chunk before processing it). None of the
  // arguments should be trusted, unless otherwise stated. We can trust that
  // |src| points to a valid memory area, but not its contents.
  //
  // This method may be called multiple times for the same chunk. In this case,
  // the original chunk's payload will be overridden and its number of fragments
  // and flags adjusted to match |num_fragments| and |chunk_flags|. The service
  // may use this to insert partial chunks (|chunk_complete = false|) before the
  // producer has committed them.
  //
  // If |chunk_complete| is |false|, we will only consider the first
  // |num_fragments - 1| fragments to be complete, since the producer may have
  // not finished writing the latest packet. Reading from a sequence will also
  // not progress past any incomplete chunks until they were rewritten with
  // |chunk_complete = true|, e.g. after a producer's commit.
  void CopyChunkUntrusted(ProducerID producer_id_trusted,
                          const ClientIdentity& client_identity_trusted,
                          WriterID writer_id,
                          ChunkID chunk_id,
                          uint16_t num_fragments,
                          uint8_t chunk_flags,
                          bool chunk_complete,
                          const uint8_t* src,
                          size_t size) override;

  // Applies a batch of |patches| to the given chunk, if the given chunk is
  // still in the buffer. Does nothing if the given ChunkID is gone.
  // Returns true if the chunk has been found and patched, false otherwise.
  // |other_patches_pending| is used to determine whether this is the only
  // batch of patches for the chunk or there is more.
  // If |other_patches_pending| == false, the chunk is marked as ready to be
  // consumed. If true, the state of the chunk is not altered.
  //
  // Note: If the producer is batching commits (see shared_memory_arbiter.h), it
  // will also attempt to do patching locally. Namely, if nested messages are
  // completed while the chunk on which they started is being batched (i.e.
  // before it has been committed to the service), the producer will apply the
  // respective patches to the batched chunk. These patches will not be sent to
  // the service - i.e. only the patches that the producer did not manage to
  // apply before committing the chunk will be applied here.
  bool TryPatchChunkContents(ProducerID,
                             WriterID,
                             ChunkID,
                             const Patch* patches,
                             size_t patches_size,
                             bool other_patches_pending) override;

  void MaybeSetUpProtoVm(const std::string& data_source_name,
                         const std::string& program_bytes,
                         uint32_t memory_limit_kb,
                         ProducerID producer_id);

  const std::vector<Vm>& GetProtoVmInstances() const { return protovms_; }

  // To read the contents of the buffer the caller needs to:
  //   BeginRead()
  //   while (ReadNextTracePacket(packet_fragments)) { ... }
  // No other calls to any other method should be interleaved between
  // BeginRead() and ReadNextTracePacket().
  // Reads in the TraceBufferV2 are NOT idempotent.
  void BeginRead() override;

  // Returns the next packet in the buffer, if any, and the producer/writer
  // identity that wrote it (as passed in the CopyChunkUntrusted() call).
  // Returns false if no packets can be read at this point.
  // If a packet was read successfully, |previous_packet_on_sequence_dropped|
  // signals whether any data loss has been detected on the sequence
  // (e.g. because its chunk was overridden due to the ring buffer wrapping or
  // due to an ABI violation), and to |false| otherwise.
  //
  // This function returns only complete packets. Specifically:
  // When there is at least one complete packet in the buffer, this function
  // returns true and populates the TracePacket argument with the boundaries of
  // each fragment for one packet.
  // TracePacket will have at least one slice when this function returns true.
  // When there are no whole packets eligible to read (e.g. we are still missing
  // fragments) this function returns false.
  // This function guarantees also that packets for a given
  // {ProducerID, WriterID} are read in FIFO order.
  // This function does not guarantee any ordering w.r.t. packets belonging to
  // different WriterID(s). For instance, given the following packets copied
  // into the buffer:
  //   {ProducerID: 1, WriterID: 1}: P1 P2 P3
  //   {ProducerID: 1, WriterID: 2}: P4 P5 P6
  //   {ProducerID: 2, WriterID: 1}: P7 P8 P9
  // The following read sequence is possible:
  //   P1, P4, P7, P2, P3, P5, P8, P9, P6
  // But the following is guaranteed to NOT happen:
  //   P1, P5, P7, P4 (P4 cannot come after P5)
  bool ReadNextTracePacket(TracePacket*,
                           PacketSequenceProperties* sequence_properties,
                           bool* previous_packet_on_sequence_dropped) override;

  // Creates a read-only clone of the trace buffer. The read iterators of the
  // new buffer will be reset, as if no Read() had been called. Calls to
  // CopyChunkUntrusted() and TryPatchChunkContents() on the returned cloned
  // TraceBuffer will CHECK().
  std::unique_ptr<TraceBuffer> CloneReadOnly() const override;

  size_t size() const override { return size_; }
  size_t used_size() const override { return used_size_; }
  size_t GetMemoryUsageBytes() const override;
  OverwritePolicy overwrite_policy() const override {
    return overwrite_policy_;
  }
  const TraceStats::BufferStats& stats() const override { return stats_; }
  const WriterStats& writer_stats() const override { return writer_stats_; }
  bool has_data() const override { return used_size_ > 0; }
  void set_read_only() override { read_only_ = true; }
  BufType buf_type() const override { return kV2; }

  void DumpForTesting();

 private:
  using Frag = internal::Frag;
  using SequenceState = internal::SequenceState;
  using ChunkSeqReader = internal::ChunkSeqReader;

  friend class TraceBufferV2Test;
  friend class internal::ChunkSeqReader;
  friend class internal::ChunkSeqIterator;

  explicit TraceBufferV2(OverwritePolicy);
  TraceBufferV2(const TraceBufferV2&) = delete;
  TraceBufferV2& operator=(const TraceBufferV2&) = delete;

  // Not using the implicit copy ctor to avoid unintended copies.
  // This tagged ctor should be used only for Clone().
  struct CloneCtor {};
  TraceBufferV2(CloneCtor, const TraceBufferV2&);

  bool Initialize(size_t size);
  TBChunk* CreateTBChunk(size_t off, size_t payload_size);
  void DeleteNextChunksFor(size_t bytes_to_clear);

  void DcheckIsAlignedAndWithinBounds(size_t off) const {
    PERFETTO_DCHECK((off & (alignof(TBChunk) - 1)) == 0);
    PERFETTO_DCHECK(off <= size_ - sizeof(TBChunk));
  }

  // This should only be used when followed by a placement new.
  TBChunk* GetTBChunkAtUnchecked(size_t off) {
    DcheckIsAlignedAndWithinBounds(off);
    return reinterpret_cast<TBChunk*>(begin() + off);
  }

  TBChunk* GetTBChunkAt(size_t off) {
    TBChunk* tbchunk = GetTBChunkAtUnchecked(off);
    PERFETTO_CHECK(tbchunk->outer_size() <= (size_ - off));

    // TODO(primiano): consider turning this into a DCHECK (and #ifdef-ing away
    // the checksum code) once TBV2 proves to be reliable.
    PERFETTO_CHECK(tbchunk->IsChecksumValid(off));
    return tbchunk;
  }

  // Can return nullptr for padding chunks (or in case of programming errors).
  SequenceState* GetSeqForChunk(const TBChunk* chunk) {
    auto it = sequences_.find(chunk->pri_wri_id);
    return it == sequences_.end() ? nullptr : &it->second;
  }

  size_t OffsetOf(const TBChunk* chunk) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(chunk);
    uintptr_t buf_start = reinterpret_cast<uintptr_t>(begin());
    PERFETTO_DCHECK(addr >= buf_start && buf_start <= addr + size_);
    return static_cast<size_t>(addr - buf_start);
  }

  void DiscardWrite();
  void DeleteStaleEmptySequences();
  void MaybeProcessOverwrittenPacketWithProtoVm(const TracePacket&, ProducerID);

  uint8_t* begin() const { return reinterpret_cast<uint8_t*>(data_.Get()); }
  uint8_t* end() const { return begin() + size_; }
  size_t size_to_end() const { return size_ - wr_; }

  base::PagedMemory data_;
  size_t size_ = 0;  // Size in bytes of |data_|.

  // High watermark. The number of bytes (<= |size_|) written into the buffer
  // before the first wraparound. This increases as data is written into the
  // buffer and then saturates at |size_|.
  size_t used_size_ = 0;

  size_t wr_ = 0;  // Write cursor (offset since start()).
  size_t rd_ = 0;  // Read cursor. Reset to wr_ on every BeginRead().
  std::optional<ChunkSeqReader> chunk_seq_reader_;

  // Statistics about buffer usage.
  TraceStats::BufferStats stats_;

  // Statistics about TraceWriters.
  WriterStats writer_stats_;

  OverwritePolicy overwrite_policy_ = kOverwrite;

  // Note: we need stable pointers for SequenceState, as they get cached in
  // BufIterator.
  std::unordered_map<ProducerAndWriterID, SequenceState> sequences_;

  // COUNT(sequences_) WHERE sequence.chunks.empty().
  // This is maintained best effort and needs revalidation against sequences_.
  size_t empty_sequences_ = 0;

  // A generation counter incremented every time BeginRead() is called.
  uint64_t read_generation_ = 0;

  // A monotonic counter incremented every time a SequenceState becomes empty.
  // This is used to sort SequenceState by least-recently cleared.
  uint64_t seq_age_ = 0;

  // This buffer is a read-only snapshot obtained via Clone(). If this is true
  // calls to CopyChunkUntrusted() and TryPatchChunkContents() will CHECK().
  bool read_only_ = false;

  // Only used when |overwrite_policy_ == kDiscard|. This is set the first time
  // a write fails because it would overwrite unread chunks.
  bool discard_writes_ = false;

  // When true disable some DCHECKs that have been put in place to detect
  // bugs in the producers. This is for tests that feed malicious inputs and
  // hence mimic a buggy producer.
  bool suppress_client_dchecks_for_testing_ = false;

  // ProtoVMs used to process overwritten packets (go/perfetto-proto-vm)
  std::vector<Vm> protovms_;

  // Used to collect slices of the overwritten packet. Note that this is a
  // member variable (instead of local) so that the memory (internal
  // std::vector<Slice>) is re-used across overwritten packets, thus involving
  // allocations only when the vector needs to be expanded (in practice only a
  // few times during the initial iterations).
  TracePacket overwritten_packet_;

  // Storage used to re-write overwritten packets (from TBv2) into contiguous
  // memory to be used as ProtoVM patches (must be continguous to be decoded
  // with protozero). Note that this is a member variable (instead of local) so
  // that the memory is re-used across overwritten packets, thus involving
  // allocations only when the storage needs to be expanded.
  std::string protovm_patch_;
};

}  // namespace perfetto

#endif  // SRC_TRACING_SERVICE_TRACE_BUFFER_V2_H_
