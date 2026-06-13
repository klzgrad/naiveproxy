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

#include "src/trace_processor/importers/proto/blob_packet_writer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/protozero/contiguous_memory_range.h"
#include "perfetto/protozero/scattered_stream_writer.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"

#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

BlobPacketWriter::BlobPacketWriter() : writer_(this) {}

BlobPacketWriter::~BlobPacketWriter() = default;

protos::pbzero::TracePacket* BlobPacketWriter::BeginPacket() {
  if (!slab_ || packet_start_ptr_ >= slab_->data() + slab_->size()) {
    slab_.reset(new TraceBlob(TraceBlob::Allocate(kSlabSize)));
    packet_start_ptr_ = slab_->data();
  }

  protozero::ContiguousMemoryRange range{packet_start_ptr_,
                                         slab_->data() + slab_->size()};
  msg_.Reset(&writer_);
  writer_.Reset(range);
  slices_.push_back(range);
  return &msg_;
}

TraceBlobView BlobPacketWriter::EndPacket() {
  PERFETTO_CHECK(!slices_.empty());
  msg_.Finalize();

  // Close the last slice with the actual end position of the packet.
  slices_.back().end = writer_.write_ptr();

  // Common case: packet fits in the single slab. Zero copies.
  if (PERFETTO_LIKELY(slices_.size() == 1)) {
    auto s = slices_.back();
    slices_.clear();
    packet_start_ptr_ = s.end;
    return {slab_, static_cast<size_t>(s.begin - slab_->data()), s.size()};
  }

  PERFETTO_CHECK(!overflow_slabs_.empty());

  // Rare: packet spans multiple slabs. Stitch into one contiguous blob.
  size_t total = 0;
  for (const auto& s : slices_) {
    total += s.size();
  }

  TraceBlob stitched = TraceBlob::Allocate(total);
  {
    uint8_t* dst = stitched.data();
    for (const auto& s : slices_) {
      memcpy(dst, s.begin, s.size());
      dst += s.size();
    }
  }

  slab_ = std::move(overflow_slabs_.front());
  packet_start_ptr_ = writer_.write_ptr();
  overflow_slabs_.clear();
  slices_.clear();
  return TraceBlobView(std::move(stitched), 0, total);
}

protozero::ContiguousMemoryRange BlobPacketWriter::GetNewBuffer() {
  PERFETTO_CHECK(!slices_.empty());

  // Close the current slice and start a new one for the new slab.
  slices_.back().end = writer_.write_ptr();

  // Allocate a new slab and add it to the overflow slabs.
  overflow_slabs_.emplace_front(new TraceBlob(TraceBlob::Allocate(kSlabSize)));

  auto& blob = *overflow_slabs_.front();
  protozero::ContiguousMemoryRange range{blob.data(),
                                         blob.data() + blob.size()};
  slices_.push_back(range);
  return range;
}

}  // namespace perfetto::trace_processor
