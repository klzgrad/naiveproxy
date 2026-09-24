/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_BLOB_PACKET_WRITER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_BLOB_PACKET_WRITER_H_

#include <cstddef>
#include <cstdint>
#include <forward_list>
#include <list>

#include "perfetto/protozero/contiguous_memory_range.h"
#include "perfetto/protozero/root_message.h"
#include "perfetto/protozero/scattered_stream_writer.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

// A zero-copy writer for synthesized ("forged") TracePackets.
//
// Several importer modules create synthetic TracePackets to decompress or
// de-intern bundled data. The previous approach using
// protozero::HeapBuffered<TracePacket> required 2 allocations + 1 memcpy per
// packet. This class reduces that to amortized 0 allocations + 0 copies by
// writing multiple packets into a single shared 4MB TraceBlob and returning
// TraceBlobViews that point into it via RefPtr.
//
// With a 4MB blob, tens of thousands of typical ~100-byte forged packets share
// a single allocation. When a slab is exhausted, a new one is allocated
// and packets that span slab boundaries are stitched (rare fallback).
//
// Usage:
//   TraceBlobView tbv = writer.WritePacket([&](auto* pkt) {
//     pkt->set_timestamp(42);
//     pkt->set_power_rails()->...;
//   });
class BlobPacketWriter : private protozero::ScatteredStreamWriter::Delegate {
 public:
  BlobPacketWriter();
  ~BlobPacketWriter() override;

  BlobPacketWriter(const BlobPacketWriter&) = delete;
  BlobPacketWriter& operator=(const BlobPacketWriter&) = delete;
  BlobPacketWriter(BlobPacketWriter&&) = delete;
  BlobPacketWriter& operator=(BlobPacketWriter&&) = delete;

  // Writes a complete TracePacket. |fn| receives a TracePacket* to populate.
  // Returns the serialized bytes as a TraceBlobView.
  // Common path (packet fits in current slab): zero copies, zero allocations.
  //
  // This is the preferred API. Use BeginPacket()/EndPacket() only when you need
  // to pass the TracePacket* to other functions.
  template <typename Fn>
  TraceBlobView WritePacket(Fn&& fn) {
    auto* pkt = BeginPacket();
    fn(pkt);
    return EndPacket();
  }

  // Begin/End API for cases where the TracePacket* needs to be passed around
  // (e.g. to helper functions that populate different parts of the packet).
  //
  // Usage:
  //   auto* pkt = writer.BeginPacket();
  //   PopulateHeader(pkt);
  //   PopulatePayload(pkt);
  //   TraceBlobView tbv = writer.EndPacket();
  protos::pbzero::TracePacket* BeginPacket();
  TraceBlobView EndPacket();

 private:
  static constexpr size_t kSlabSize = size_t{4} * 1024 * 1024;  // 4MB

  protozero::ContiguousMemoryRange GetNewBuffer() override;

  protozero::ScatteredStreamWriter writer_;
  protozero::RootMessage<protos::pbzero::TracePacket> msg_;

  RefPtr<TraceBlob> slab_;
  uint8_t* packet_start_ptr_ = nullptr;

  // Overflow slabs allocated when a packet spans the current slab boundary.
  // Empty in the common case. The front element is always the current write
  // slab when non-empty; slab_ is the slab where the packet started.
  std::forward_list<RefPtr<TraceBlob>> overflow_slabs_;

  // Slices of the current packet. Used to stitch packets that span slab
  // boundaries.
  std::list<protozero::ContiguousMemoryRange> slices_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_BLOB_PACKET_WRITER_H_
