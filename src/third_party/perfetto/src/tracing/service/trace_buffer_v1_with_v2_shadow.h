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

#ifndef SRC_TRACING_SERVICE_TRACE_BUFFER_V1_WITH_V2_SHADOW_H_
#define SRC_TRACING_SERVICE_TRACE_BUFFER_V1_WITH_V2_SHADOW_H_

#include <memory>
#include <unordered_set>

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/tracing/service/trace_buffer.h"

namespace perfetto {

class TracePacket;

// ****************************************************************************
// * THIS IS A TEMPORARY CLASS FOR TESTING PURPOSES ONLY.                     *
// * It will be removed once TraceBufferV2 is validated and fully rolled out. *
// ****************************************************************************
//
// A wrapper around TraceBufferV1 that also maintains a TraceBufferV2 shadow
// buffer for comparison purposes. All data is written to both buffers, but
// only V1 data is returned during readback. Comparison statistics are
// computed to validate that V2 returns equivalent data.
//
// The comparison works as follows:
// - CopyChunkUntrusted/TryPatchChunkContents: forwarded to both V1 and V2
// - BeginRead: forwarded to both. V2 is eagerly read to completion and packet
//   hashes are stored.
// - ReadNextTracePacket: forwarded to V1 only. Each V1 packet's hash is
//   compared against V2 hashes.
// - stats(): returns V1's stats with shadow comparison fields populated.
class TraceBufferV1WithV2Shadow : public TraceBuffer {
 public:
  static std::unique_ptr<TraceBufferV1WithV2Shadow> Create(size_t size_in_bytes,
                                                           OverwritePolicy);

  ~TraceBufferV1WithV2Shadow() override;

  // TraceBuffer implementation - forwards to both V1 and V2.
  void CopyChunkUntrusted(ProducerID producer_id_trusted,
                          const ClientIdentity& client_identity_trusted,
                          WriterID writer_id,
                          ChunkID chunk_id,
                          uint16_t num_fragments,
                          uint8_t chunk_flags,
                          bool chunk_complete,
                          const uint8_t* src,
                          size_t size) override;

  bool TryPatchChunkContents(ProducerID,
                             WriterID,
                             ChunkID,
                             const Patch* patches,
                             size_t patches_size,
                             bool other_patches_pending) override;

  // BeginRead forwards to both. V2 is eagerly read and hashes are stored.
  void BeginRead() override;

  // ReadNextTracePacket forwards to V1 only. Hash comparison is performed.
  bool ReadNextTracePacket(TracePacket*,
                           PacketSequenceProperties* sequence_properties,
                           bool* previous_packet_on_sequence_dropped) override;

  std::unique_ptr<TraceBuffer> CloneReadOnly() const override;

  // Stats returns V1's stats with shadow comparison fields populated.
  const TraceStats::BufferStats& stats() const override;

  // These all forward to V1 only.
  void set_read_only() override;
  const WriterStats& writer_stats() const override;
  size_t size() const override;
  size_t used_size() const override;
  size_t GetMemoryUsageBytes() const override;
  OverwritePolicy overwrite_policy() const override;
  bool has_data() const override;
  BufType buf_type() const override { return kV1WithV2Shadow; }

 private:
  TraceBufferV1WithV2Shadow();

  // Updates the shadow comparison stats in stats_.
  void UpdateShadowStats() const;

  static constexpr uint8_t kSeenInV1 = 1 << 0;
  static constexpr uint8_t kSeenInV2 = 1 << 1;

  std::unique_ptr<TraceBuffer> v1_;
  std::unique_ptr<TraceBuffer> v2_;

  struct HashPacketCounts {
    uint16_t seen_in_v1 = 0;
    uint16_t seen_in_v2 = 0;
  };
  base::FlatHashMap<uint64_t,
                    HashPacketCounts,
                    base::AlreadyHashed<uint64_t>,
                    base::QuadraticProbe,
                    /*AppendOnly=*/true>
      packet_hashes_;
  uint64_t packets_seen_ = 0;

  // Patch statistics.
  uint64_t patches_attempted_ = 0;
  uint64_t v1_patches_succeeded_ = 0;
  uint64_t v2_patches_succeeded_ = 0;

  // Cached stats that combines V1 stats with shadow comparison results.
  mutable TraceStats::BufferStats stats_;
};

}  // namespace perfetto

#endif  // SRC_TRACING_SERVICE_TRACE_BUFFER_V1_WITH_V2_SHADOW_H_
