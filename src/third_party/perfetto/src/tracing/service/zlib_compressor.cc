/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/tracing/service/zlib_compressor.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
#error "Zlib must be enabled to compile this file."
#endif

#include <zlib.h>

#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

namespace {

struct Preamble {
  uint32_t size;
  std::array<uint8_t, 16> buf;
};

template <uint32_t id>
Preamble GetPreamble(size_t sz) {
  Preamble preamble;
  uint8_t* ptr = preamble.buf.data();
  constexpr uint32_t tag = protozero::proto_utils::MakeTagLengthDelimited(id);
  ptr = protozero::proto_utils::WriteVarInt(tag, ptr);
  ptr = protozero::proto_utils::WriteVarInt(sz, ptr);
  preamble.size =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ptr) -
                            reinterpret_cast<uintptr_t>(preamble.buf.data()));
  PERFETTO_DCHECK(preamble.size < preamble.buf.size());
  return preamble;
}

Slice PreambleToSlice(const Preamble& preamble) {
  Slice slice = Slice::Allocate(preamble.size);
  memcpy(slice.own_data(), preamble.buf.data(), preamble.size);
  return slice;
}

// A compressor for `TracePacket`s that uses zlib. The class is exposed for
// testing.
class ZlibPacketCompressor {
 public:
  ZlibPacketCompressor();
  ~ZlibPacketCompressor();

  // Can be called multiple times, before Finish() is called.
  void PushPacket(const TracePacket& packet);

  // Returned the compressed data. Can be called at most once. After this call,
  // the object is unusable (PushPacket should not be called) and must be
  // destroyed.
  TracePacket Finish();

 private:
  void PushData(const void* data, uint32_t size);
  void NewOutputSlice();
  void PushCurSlice();

  z_stream stream_;
  size_t total_new_slices_size_ = 0;
  std::vector<Slice> new_slices_;
  std::unique_ptr<uint8_t[]> cur_slice_;
};

ZlibPacketCompressor::ZlibPacketCompressor() {
  memset(&stream_, 0, sizeof(stream_));
  int status = deflateInit(&stream_, 6);
  PERFETTO_CHECK(status == Z_OK);
}

ZlibPacketCompressor::~ZlibPacketCompressor() {
  int status = deflateEnd(&stream_);
  PERFETTO_CHECK(status == Z_OK);
}

void ZlibPacketCompressor::PushPacket(const TracePacket& packet) {
  // We need to be able to tokenize packets in the compressed stream, so we
  // prefix a proto preamble to each packet. The compressed stream looks like a
  // valid Trace proto.
  Preamble preamble =
      GetPreamble<protos::pbzero::Trace::kPacketFieldNumber>(packet.size());
  PushData(preamble.buf.data(), preamble.size);
  for (const Slice& slice : packet.slices()) {
    PushData(slice.start, static_cast<uint32_t>(slice.size));
  }
}

void ZlibPacketCompressor::PushData(const void* data, uint32_t size) {
  stream_.next_in = const_cast<Bytef*>(static_cast<const Bytef*>(data));
  stream_.avail_in = static_cast<uInt>(size);
  while (stream_.avail_in != 0) {
    if (stream_.avail_out == 0) {
      NewOutputSlice();
    }
    int status = deflate(&stream_, Z_NO_FLUSH);
    PERFETTO_CHECK(status == Z_OK);
  }
}

TracePacket ZlibPacketCompressor::Finish() {
  for (;;) {
    int status = deflate(&stream_, Z_FINISH);
    if (status == Z_STREAM_END)
      break;
    PERFETTO_CHECK(status == Z_OK || status == Z_BUF_ERROR);
    NewOutputSlice();
  }

  PushCurSlice();

  TracePacket packet;
  packet.AddSlice(PreambleToSlice(
      GetPreamble<protos::pbzero::TracePacket::kCompressedPacketsFieldNumber>(
          total_new_slices_size_)));
  for (auto& slice : new_slices_) {
    packet.AddSlice(std::move(slice));
  }
  return packet;
}

void ZlibPacketCompressor::NewOutputSlice() {
  PushCurSlice();
  cur_slice_ = std::make_unique<uint8_t[]>(kZlibCompressSliceSize);
  stream_.next_out = reinterpret_cast<Bytef*>(cur_slice_.get());
  stream_.avail_out = kZlibCompressSliceSize;
}

void ZlibPacketCompressor::PushCurSlice() {
  if (cur_slice_) {
    total_new_slices_size_ += kZlibCompressSliceSize - stream_.avail_out;
    new_slices_.push_back(Slice::TakeOwnership(
        std::move(cur_slice_), kZlibCompressSliceSize - stream_.avail_out));
  }
}

}  // namespace

void ZlibCompressFn(std::vector<TracePacket>* packets) {
  if (packets->empty()) {
    return;
  }

  ZlibPacketCompressor stream;

  for (const TracePacket& packet : *packets) {
    stream.PushPacket(packet);
  }

  TracePacket packet = stream.Finish();

  packets->clear();
  packets->push_back(std::move(packet));
}

}  // namespace perfetto
