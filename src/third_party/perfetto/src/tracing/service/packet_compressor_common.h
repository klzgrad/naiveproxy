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

#ifndef SRC_TRACING_SERVICE_PACKET_COMPRESSOR_COMMON_H_
#define SRC_TRACING_SERVICE_PACKET_COMPRESSOR_COMMON_H_

#include <array>
#include <cstdint>
#include <cstring>

#include "perfetto/base/logging.h"
#include "perfetto/ext/tracing/core/slice.h"
#include "perfetto/protozero/proto_utils.h"

namespace perfetto {
namespace packet_compressor {

// TODO(sashwinbalaji): Extract the output-slice buffering and packet assembly
// shared by the zlib and zstd compressors into this header, so each codec is
// just its own streaming loop.

// Size of each compressed output slice. Mirrors the service's
// kMaxTracePacketSliceSize.
inline constexpr size_t kCompressSliceSize = 128 * 1024 - 512;

// Builds the proto preamble (field tag + length) that the zlib and zstd packet
// compressors prefix to each TracePacket, so the compressed stream itself
// parses as a valid Trace proto.
struct Preamble {
  uint32_t size;
  std::array<uint8_t, 16> buf;
};

template <uint32_t id>
Preamble GetPreamble(size_t sz) {
  Preamble preamble{};
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

inline Slice PreambleToSlice(const Preamble& preamble) {
  Slice slice = Slice::Allocate(preamble.size);
  memcpy(slice.own_data(), preamble.buf.data(), preamble.size);
  return slice;
}

}  // namespace packet_compressor
}  // namespace perfetto

#endif  // SRC_TRACING_SERVICE_PACKET_COMPRESSOR_COMMON_H_
