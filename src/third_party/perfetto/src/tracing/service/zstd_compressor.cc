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

#include "src/tracing/service/zstd_compressor.h"

#include "perfetto/base/build_config.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ZSTD)

#include <memory>

#include <zstd.h>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/scoped_file.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/tracing/service/packet_compressor_common.h"

namespace perfetto {

namespace {

using packet_compressor::GetPreamble;
using packet_compressor::kCompressSliceSize;
using packet_compressor::Preamble;
using packet_compressor::PreambleToSlice;

// ScopedResource's close function must return int; ZSTD_freeCStream returns
// size_t, so wrap it. It only fails for a static-allocated stream, not the
// heap one ZSTD_createCStream gives us, so returning 0 is safe.
inline int ZstdFreeCStream(ZSTD_CStream* cstream) {
  ZSTD_freeCStream(cstream);
  return 0;
}
using ScopedZstdCStream = base::ScopedResource<ZSTD_CStream*,
                                               ZstdFreeCStream,
                                               /*InvalidValue=*/nullptr,
                                               /*CheckClose=*/false>;

// A compressor for `TracePacket`s that uses zstd's streaming API: data is fed
// in with ZSTD_compressStream2(ZSTD_e_continue) and the frame is finalized with
// ZSTD_e_end, emitting output in fixed-size slices (kCompressSliceSize).
// zstd API reference: https://facebook.github.io/zstd/zstd_manual.html
class ZstdPacketCompressor {
 public:
  explicit ZstdPacketCompressor(int level);

  // Can be called multiple times, before Finish() is called.
  void PushPacket(const TracePacket& packet);

  // Returns the compressed data. Can be called at most once. After this call,
  // the object is unusable (PushPacket should not be called) and must be
  // destroyed.
  TracePacket Finish();

 private:
  void PushData(const void* data, uint32_t size);
  void NewOutputSlice();
  void PushCurSlice();

  ScopedZstdCStream cstream_;
  // Points into `cur_slice_`. Zero-initialized so that the first compression
  // call observes a full output buffer and allocates the initial slice.
  ZSTD_outBuffer out_{/*dst=*/nullptr, /*size=*/0, /*pos=*/0};
  size_t total_new_slices_size_ = 0;
  std::vector<Slice> new_slices_;
  std::unique_ptr<uint8_t[]> cur_slice_;
};

ZstdPacketCompressor::ZstdPacketCompressor(int level) {
  cstream_.reset(ZSTD_createCStream());
  PERFETTO_CHECK(cstream_);
  // zstd maps 0 to its default level, clamps levels above its max, and treats
  // negatives as fast modes, so any int is safe to pass through here.
  size_t rc =
      ZSTD_CCtx_setParameter(cstream_.get(), ZSTD_c_compressionLevel, level);
  PERFETTO_CHECK(!ZSTD_isError(rc));
}

void ZstdPacketCompressor::PushPacket(const TracePacket& packet) {
  // Prefix each packet with its proto preamble so the compressed stream itself
  // parses as a valid Trace proto, and its packets can be tokenized back out.
  Preamble preamble =
      GetPreamble<protos::pbzero::Trace::kPacketFieldNumber>(packet.size());
  PushData(preamble.buf.data(), preamble.size);
  for (const Slice& slice : packet.slices()) {
    PushData(slice.start, static_cast<uint32_t>(slice.size));
  }
}

void ZstdPacketCompressor::PushData(const void* data, uint32_t size) {
  // ZSTD_e_continue hands data to the encoder, which buffers and emits at its
  // own discretion. It may not consume all input in one call (e.g. when the
  // output slice fills), so loop until `in` is drained, giving it a fresh slice
  // whenever `out_` is full.
  ZSTD_inBuffer in = {/*src=*/data, /*size=*/size, /*pos=*/0};
  while (in.pos < in.size) {
    if (out_.pos == out_.size) {
      NewOutputSlice();
    }
    size_t rc =
        ZSTD_compressStream2(cstream_.get(), &out_, &in, ZSTD_e_continue);
    PERFETTO_CHECK(!ZSTD_isError(rc));
  }
}

TracePacket ZstdPacketCompressor::Finish() {
  // ZSTD_e_end flushes buffered data and writes the frame epilogue. Per the
  // contract, keep calling (draining `out_` into new slices) until it reports 0
  // bytes remaining.
  size_t remaining;
  do {
    if (out_.pos == out_.size) {
      NewOutputSlice();
    }
    ZSTD_inBuffer in = {/*src=*/nullptr, /*size=*/0, /*pos=*/0};
    remaining = ZSTD_compressStream2(cstream_.get(), &out_, &in, ZSTD_e_end);
    PERFETTO_CHECK(!ZSTD_isError(remaining));
  } while (remaining != 0);

  PushCurSlice();

  TracePacket packet;
  packet.AddSlice(PreambleToSlice(
      GetPreamble<
          protos::pbzero::TracePacket::kZstdCompressedPacketsFieldNumber>(
          total_new_slices_size_)));
  for (auto& slice : new_slices_) {
    packet.AddSlice(std::move(slice));
  }
  return packet;
}

void ZstdPacketCompressor::NewOutputSlice() {
  PushCurSlice();
  cur_slice_ = std::make_unique<uint8_t[]>(kCompressSliceSize);
  out_.dst = cur_slice_.get();
  out_.size = kCompressSliceSize;
  out_.pos = 0;
}

void ZstdPacketCompressor::PushCurSlice() {
  if (!cur_slice_) {
    return;
  }
  total_new_slices_size_ += out_.pos;
  new_slices_.push_back(Slice::TakeOwnership(std::move(cur_slice_), out_.pos));
}

}  // namespace

void ZstdCompressFn(std::vector<TracePacket>* packets, int level) {
  if (packets->empty()) {
    return;
  }

  ZstdPacketCompressor stream(level);

  for (const TracePacket& packet : *packets) {
    stream.PushPacket(packet);
  }

  TracePacket packet = stream.Finish();

  packets->clear();
  packets->push_back(std::move(packet));
}

}  // namespace perfetto

#endif  // PERFETTO_BUILDFLAG(PERFETTO_ZSTD)
