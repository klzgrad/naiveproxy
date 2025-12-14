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

#ifndef SRC_TRACING_SERVICE_TRACE_BUFFER_H_
#define SRC_TRACING_SERVICE_TRACE_BUFFER_H_

#include <stdint.h>
#include <array>
#include <memory>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/core/client_identity.h"
#include "perfetto/ext/tracing/core/trace_stats.h"
#include "src/tracing/service/histogram.h"

namespace perfetto {

class TracePacket;
class TraceBuffer_WriterStats;

// Virtual interface for trace buffers to enable multiple implementations.
// This interface defines the minimal surface used by the tracing service.
class TraceBuffer {
 public:
  using WriterStats = TraceBuffer_WriterStats;

  // See comment in the header above.
  enum OverwritePolicy { kOverwrite, kDiscard };

  // Argument for out-of-band patches applied through TryPatchChunkContents().
  struct Patch {
    // From SharedMemoryABI::kPacketHeaderSize.
    static constexpr size_t kSize = 4;

    size_t offset_untrusted;
    std::array<uint8_t, kSize> data;
  };

  // Identifiers that are constant for a packet sequence.
  struct PacketSequenceProperties {
    ProducerID producer_id_trusted;
    ClientIdentity client_identity_trusted;
    WriterID writer_id;

    uid_t producer_uid_trusted() const { return client_identity_trusted.uid(); }
    pid_t producer_pid_trusted() const { return client_identity_trusted.pid(); }
  };

  virtual ~TraceBuffer();

  // Copies a Chunk from a producer Shared Memory Buffer into the trace buffer.
  virtual void CopyChunkUntrusted(ProducerID producer_id_trusted,
                                  const ClientIdentity& client_identity_trusted,
                                  WriterID writer_id,
                                  ChunkID chunk_id,
                                  uint16_t num_fragments,
                                  uint8_t chunk_flags,
                                  bool chunk_complete,
                                  const uint8_t* src,
                                  size_t size) = 0;

  // Applies a batch of |patches| to the given chunk, if the given chunk is
  // still in the buffer. Does nothing if the given ChunkID is gone.
  // Returns true if the chunk has been found and patched, false otherwise.
  virtual bool TryPatchChunkContents(ProducerID,
                                     WriterID,
                                     ChunkID,
                                     const Patch* patches,
                                     size_t patches_size,
                                     bool other_patches_pending) = 0;

  // To read the contents of the buffer the caller needs to:
  //   BeginRead()
  //   while (ReadNextTracePacket(packet_fragments)) { ... }
  // No other calls to any other method should be interleaved between
  // BeginRead() and ReadNextTracePacket().
  // Reads in the TraceBuffer are NOT idempotent.
  virtual void BeginRead() = 0;

  // Returns the next packet in the buffer, if any, and the producer_id,
  // producer_uid, and writer_id of the producer/writer that wrote it.
  // Returns false if no packets can be read at this point.
  virtual bool ReadNextTracePacket(
      TracePacket*,
      PacketSequenceProperties* sequence_properties,
      bool* previous_packet_on_sequence_dropped) = 0;

  // Creates a read-only clone of the trace buffer. The read iterators of the
  // new buffer will be reset, as if no Read() had been called.
  virtual std::unique_ptr<TraceBuffer> CloneReadOnly() const = 0;

  virtual void set_read_only() = 0;
  virtual const TraceStats::BufferStats& stats() const = 0;
  virtual const WriterStats& writer_stats() const = 0;
  virtual size_t size() const = 0;
  virtual size_t used_size() const = 0;
  virtual OverwritePolicy overwrite_policy() const = 0;
  virtual bool has_data() const = 0;

  // Exposed for test/fake_packet.{cc,h}.
  static inline constexpr size_t InlineChunkHeaderSize = 16;
};

class TraceBuffer_WriterStats {
 public:
  using WriterBuckets =
      Histogram<8, 32, 128, 512, 1024, 2048, 4096, 8192, 12288, 16384>;
  using WriterStatsMap = base::FlatHashMap<ProducerAndWriterID,
                                           WriterBuckets,
                                           std::hash<ProducerAndWriterID>,
                                           base::QuadraticProbe,
                                           /*AppendOnly=*/true>;

  void Insert(ProducerAndWriterID key, HistValue val) {
    map_.Insert(key, {}).first->Add(val);
  }

  WriterStatsMap::Iterator GetIterator() const { return map_.GetIterator(); }

 private:
  WriterStatsMap map_;
};

}  // namespace perfetto

#endif  // SRC_TRACING_SERVICE_TRACE_BUFFER_H_
