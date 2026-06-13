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

#include "src/tracing/service/trace_buffer_v1_with_v2_shadow.h"

#include "perfetto/ext/base/fnv_hash.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "src/tracing/service/trace_buffer_v1.h"
#include "src/tracing/service/trace_buffer_v2.h"

// ****************************************************************************
// * THIS IS A TEMPORARY CLASS FOR TESTING PURPOSES ONLY.                     *
// * It will be removed once TraceBufferV2 is validated and fully rolled out. *
// * See the header file for detailed documentation.                          *
// ****************************************************************************

namespace perfetto {

namespace {

// Stop processing hashes after these many packets. After this point we lose
// stats accuracy for the sake of keeping memory bounded.
constexpr size_t kMaxPacketHashes = 1000000;

uint64_t ComputePacketHash(
    const TracePacket& packet,
    const TraceBuffer::PacketSequenceProperties& seq_props) {
  base::MurmurHashCombiner hasher;
  for (const Slice& slice : packet.slices()) {
    hasher.Combine(std::string_view(reinterpret_cast<const char*>(slice.start),
                                    slice.size));
  }
  hasher.Combine(seq_props.producer_id_trusted);
  hasher.Combine(seq_props.writer_id);
  return hasher.digest();
}

void IncrementWithSaturation(uint16_t* v) {
  *v += *v < UINT16_MAX ? 1 : 0;
}

}  // namespace

TraceBufferV1WithV2Shadow::TraceBufferV1WithV2Shadow() = default;
TraceBufferV1WithV2Shadow::~TraceBufferV1WithV2Shadow() = default;

// static
std::unique_ptr<TraceBufferV1WithV2Shadow> TraceBufferV1WithV2Shadow::Create(
    size_t size_in_bytes,
    OverwritePolicy policy) {
  auto v1 = TraceBufferV1::Create(size_in_bytes, policy);
  auto v2 = TraceBufferV2::Create(size_in_bytes, policy);
  if (!v1 || !v2)
    return nullptr;

  std::unique_ptr<TraceBufferV1WithV2Shadow> instance(
      new TraceBufferV1WithV2Shadow());
  instance->v1_ = std::move(v1);
  instance->v2_ = std::move(v2);
  return instance;
}

void TraceBufferV1WithV2Shadow::CopyChunkUntrusted(
    ProducerID producer_id_trusted,
    const ClientIdentity& client_identity_trusted,
    WriterID writer_id,
    ChunkID chunk_id,
    uint16_t num_fragments,
    uint8_t chunk_flags,
    bool chunk_complete,
    const uint8_t* src,
    size_t size) {
  v1_->CopyChunkUntrusted(producer_id_trusted, client_identity_trusted,
                          writer_id, chunk_id, num_fragments, chunk_flags,
                          chunk_complete, src, size);
  v2_->CopyChunkUntrusted(producer_id_trusted, client_identity_trusted,
                          writer_id, chunk_id, num_fragments, chunk_flags,
                          chunk_complete, src, size);
}

bool TraceBufferV1WithV2Shadow::TryPatchChunkContents(
    ProducerID producer_id,
    WriterID writer_id,
    ChunkID chunk_id,
    const Patch* patches,
    size_t patches_size,
    bool other_patches_pending) {
  patches_attempted_++;
  bool v1_result =
      v1_->TryPatchChunkContents(producer_id, writer_id, chunk_id, patches,
                                 patches_size, other_patches_pending);
  bool v2_result =
      v2_->TryPatchChunkContents(producer_id, writer_id, chunk_id, patches,
                                 patches_size, other_patches_pending);
  if (v1_result)
    v1_patches_succeeded_++;
  if (v2_result)
    v2_patches_succeeded_++;
  return v1_result;
}

void TraceBufferV1WithV2Shadow::BeginRead() {
  v1_->BeginRead();

  if (packets_seen_ > kMaxPacketHashes)
    return;

  // Read all V2 packets eagerly and store their hashes.
  v2_->BeginRead();

  for (;;) {
    TracePacket packet;
    PacketSequenceProperties seq_props{};
    bool prev_dropped = false;
    if (!v2_->ReadNextTracePacket(&packet, &seq_props, &prev_dropped))
      break;
    auto hash = ComputePacketHash(packet, seq_props);
    IncrementWithSaturation(&packet_hashes_[hash].seen_in_v2);
  }
}

bool TraceBufferV1WithV2Shadow::ReadNextTracePacket(
    TracePacket* packet,
    PacketSequenceProperties* sequence_properties,
    bool* previous_packet_on_sequence_dropped) {
  bool result = v1_->ReadNextTracePacket(packet, sequence_properties,
                                         previous_packet_on_sequence_dropped);
  if (result && packets_seen_ < kMaxPacketHashes) {
    auto hash = ComputePacketHash(*packet, *sequence_properties);
    IncrementWithSaturation(&packet_hashes_[hash].seen_in_v1);
    ++packets_seen_;
  }
  return result;
}

std::unique_ptr<TraceBuffer> TraceBufferV1WithV2Shadow::CloneReadOnly() const {
  auto v1_clone = v1_->CloneReadOnly();
  auto v2_clone = v2_->CloneReadOnly();
  if (!v1_clone || !v2_clone)
    return nullptr;

  std::unique_ptr<TraceBufferV1WithV2Shadow> clone(
      new TraceBufferV1WithV2Shadow());
  clone->v1_ = std::move(v1_clone);
  clone->v2_ = std::move(v2_clone);
  // Carry over patch stats; clone starts with fresh comparison state.
  clone->patches_attempted_ = patches_attempted_;
  clone->v1_patches_succeeded_ = v1_patches_succeeded_;
  clone->v2_patches_succeeded_ = v2_patches_succeeded_;
  return clone;
}

const TraceStats::BufferStats& TraceBufferV1WithV2Shadow::stats() const {
  UpdateShadowStats();
  return stats_;
}

void TraceBufferV1WithV2Shadow::UpdateShadowStats() const {
  // Copy V1's stats as the base.
  stats_ = v1_->stats();

  // Count packets.
  uint64_t packets_in_both = 0;
  uint64_t packets_only_v1 = 0;
  uint64_t packets_only_v2 = 0;
  for (auto it = packet_hashes_.GetIterator(); it; ++it) {
    HashPacketCounts& counts = it.value();
    if (counts.seen_in_v1 <= counts.seen_in_v2) {
      packets_in_both += counts.seen_in_v1;
      packets_only_v2 += counts.seen_in_v2 - counts.seen_in_v1;
    } else {
      packets_in_both += counts.seen_in_v2;
      packets_only_v1 += counts.seen_in_v1 - counts.seen_in_v2;
    }
  }

  // Populate shadow buffer stats.
  auto* shadow_stats = stats_.mutable_shadow_buffer_stats();
  shadow_stats->set_stats_version(2);
  shadow_stats->set_packets_seen(packets_seen_);
  shadow_stats->set_packets_in_both(packets_in_both);
  shadow_stats->set_packets_only_v1(packets_only_v1);
  shadow_stats->set_packets_only_v2(packets_only_v2);
  shadow_stats->set_patches_attempted(patches_attempted_);
  shadow_stats->set_v1_patches_succeeded(v1_patches_succeeded_);
  shadow_stats->set_v2_patches_succeeded(v2_patches_succeeded_);
}

void TraceBufferV1WithV2Shadow::set_read_only() {
  v1_->set_read_only();
  v2_->set_read_only();
}

const TraceBuffer::WriterStats& TraceBufferV1WithV2Shadow::writer_stats()
    const {
  return v1_->writer_stats();
}

size_t TraceBufferV1WithV2Shadow::size() const {
  return v1_->size();
}

size_t TraceBufferV1WithV2Shadow::used_size() const {
  return v1_->used_size();
}

size_t TraceBufferV1WithV2Shadow::GetMemoryUsageBytes() const {
  return v1_->GetMemoryUsageBytes() + v2_->GetMemoryUsageBytes();
}

TraceBuffer::OverwritePolicy TraceBufferV1WithV2Shadow::overwrite_policy()
    const {
  return v1_->overwrite_policy();
}

bool TraceBufferV1WithV2Shadow::has_data() const {
  return v1_->has_data();
}

}  // namespace perfetto
