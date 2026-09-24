/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/perfetto_cmd/packet_writer.h"

#include <array>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>

#include "perfetto/base/build_config.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/paged_memory.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/protozero/proto_utils.h"
#include "protos/perfetto/trace/trace.pbzero.h"

namespace perfetto {
namespace {

using protozero::proto_utils::MakeTagLengthDelimited;
using protozero::proto_utils::WriteVarInt;
using Preamble = std::array<char, 16>;

template <uint32_t id>
size_t GetPreamble(size_t sz, Preamble* preamble) {
  uint8_t* ptr = reinterpret_cast<uint8_t*>(preamble->data());
  constexpr uint32_t tag = MakeTagLengthDelimited(id);
  ptr = WriteVarInt(tag, ptr);
  ptr = WriteVarInt(sz, ptr);
  size_t preamble_size = reinterpret_cast<uintptr_t>(ptr) -
                         reinterpret_cast<uintptr_t>(preamble->data());
  PERFETTO_DCHECK(preamble_size < preamble->size());
  return preamble_size;
}

}  // namespace

PacketWriter::~PacketWriter() {
  fflush(fd_);
}

bool PacketWriter::WritePacket(const TracePacket& packet) {
  Preamble preamble;
  size_t size = GetPreamble<protos::pbzero::Trace::kPacketFieldNumber>(
      packet.size(), &preamble);
  if (fwrite(preamble.data(), 1, size, fd_) != size)
    return false;
  for (const Slice& slice : packet.slices()) {
    if (fwrite(reinterpret_cast<const char*>(slice.start), 1, slice.size,
               fd_) != slice.size) {
      return false;
    }
  }

  return true;
}

}  // namespace perfetto
