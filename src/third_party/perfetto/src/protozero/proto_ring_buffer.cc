/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "perfetto/ext/protozero/proto_ring_buffer.h"

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/paged_memory.h"
#include "perfetto/protozero/proto_utils.h"

namespace protozero {

namespace {
constexpr size_t kGrowBytes = 128 * 1024;

inline ProtoRingBuffer::Message FramingError() {
  ProtoRingBuffer::Message msg{};
  msg.fatal_framing_error = true;
  return msg;
}

// Tries to decode a length-delimited proto field from |start|.
// Returns a valid boundary if the preamble is valid and the length is within
// |end|, or an invalid message otherwise.
ProtoRingBuffer::Message TryReadProtoMessage(const uint8_t* start,
                                             const uint8_t* end) {
  namespace proto_utils = protozero::proto_utils;
  uint64_t field_tag = 0;
  auto* start_of_len = proto_utils::ParseVarInt(start, end, &field_tag);
  if (start_of_len == start)
    return ProtoRingBuffer::Message{};  // Not enough data.

  const uint32_t tag = field_tag & 0x07;
  if (tag !=
      static_cast<uint32_t>(proto_utils::ProtoWireType::kLengthDelimited)) {
    PERFETTO_ELOG("RPC framing error, unexpected msg tag 0x%xu", tag);
    return FramingError();
  }

  uint64_t msg_len = 0;
  auto* start_of_msg = proto_utils::ParseVarInt(start_of_len, end, &msg_len);
  if (start_of_msg == start_of_len)
    return ProtoRingBuffer::Message{};  // Not enough data.

  if (msg_len > ProtoRingBuffer::kMaxMsgSize) {
    PERFETTO_ELOG("RPC framing error, message too large (%" PRIu64 " > %zu)",
                  msg_len, ProtoRingBuffer::kMaxMsgSize);
    return FramingError();
  }

  if (start_of_msg + msg_len > end)
    return ProtoRingBuffer::Message{};  // Not enough data.

  ProtoRingBuffer::Message msg{};
  msg.start = start_of_msg;
  msg.len = static_cast<uint32_t>(msg_len);
  msg.field_id = static_cast<uint32_t>(field_tag >> 3);
  return msg;
}

}  // namespace

RingBufferMessageReader::RingBufferMessageReader()
    : buf_(perfetto::base::PagedMemory::Allocate(kGrowBytes)) {}
RingBufferMessageReader::~RingBufferMessageReader() = default;

void RingBufferMessageReader::Append(const void* data_void, size_t data_len) {
  if (failed_)
    return;
  const uint8_t* data = static_cast<const uint8_t*>(data_void);
  PERFETTO_DCHECK(wr_ <= buf_.size());
  PERFETTO_DCHECK(wr_ >= rd_);

  // If the last call to ReadMessage() consumed all the data in the buffer and
  // there are no incomplete messages pending, restart from the beginning rather
  // than keep ringing. This is the most common case.
  if (rd_ == wr_)
    rd_ = wr_ = 0;

  // The caller is expected to always issue a ReadMessage() after each Append().
  PERFETTO_CHECK(!fastpath_.valid());
  if (rd_ == wr_) {
    auto msg = TryReadMessage(data, data + data_len);
    if (msg.valid() && msg.end() == (data + data_len)) {
      // Fastpath: in many cases, the underlying stream will effectively
      // preserve the atomicity of messages for most small messages.
      // In this case we can avoid the extra buf_ roundtrip and just pass a
      // pointer to |data| + (proto preamble len).
      // The next call to ReadMessage)= will return |fastpath_|.
      fastpath_ = std::move(msg);
      return;
    }
  }

  size_t avail = buf_.size() - wr_;
  if (data_len > avail) {
    // This whole section should be hit extremely rarely.

    // Try first just recompacting the buffer by moving everything to the left.
    // This can happen if we received "a message and a bit" on each Append call
    // so we ended pup in a situation like:
    // buf_: [unused space] [msg1 incomplete]
    //                      ^rd_             ^wr_
    //
    // After recompaction:
    // buf_: [msg1 incomplete]
    //       ^rd_             ^wr_
    uint8_t* buf = static_cast<uint8_t*>(buf_.Get());
    memmove(&buf[0], &buf[rd_], wr_ - rd_);
    avail += rd_;
    wr_ -= rd_;
    rd_ = 0;
    if (data_len > avail) {
      // The compaction didn't free up enough space and we need to expand the
      // ring buffer. Yes, we could have detected this earlier and split the
      // code paths, rather than first compacting and then realizing it wasn't
      // sufficient. However, that would make the code harder to reason about,
      // creating code paths that are nearly never hit, hence making it more
      // likely to accumulate bugs in future. All this is very rare.
      size_t new_size = buf_.size();
      while (data_len > new_size - wr_)
        new_size += kGrowBytes;
      if (new_size > kMaxMsgSize * 2) {
        failed_ = true;
        return;
      }
      auto new_buf = perfetto::base::PagedMemory::Allocate(new_size);
      memcpy(new_buf.Get(), buf_.Get(), buf_.size());
      buf_ = std::move(new_buf);
      avail = new_size - wr_;
      // No need to touch rd_ / wr_ cursors.
    }
  }

  // Append the received data at the end of the ring buffer.
  uint8_t* buf = static_cast<uint8_t*>(buf_.Get());
  memcpy(&buf[wr_], data, data_len);
  wr_ += data_len;
}

RingBufferMessageReader::Message RingBufferMessageReader::ReadMessage() {
  if (failed_)
    return FramingError();

  if (fastpath_.valid()) {
    // The fastpath can only be hit when the buffer is empty.
    PERFETTO_CHECK(rd_ == wr_);
    auto msg = std::move(fastpath_);
    fastpath_ = Message{};
    return msg;
  }

  uint8_t* buf = static_cast<uint8_t*>(buf_.Get());

  PERFETTO_DCHECK(rd_ <= wr_);
  if (rd_ >= wr_)
    return Message{};  // Completely empty.

  auto msg = TryReadMessage(&buf[rd_], &buf[wr_]);
  if (!msg.valid()) {
    failed_ = failed_ || msg.fatal_framing_error;
    return msg;  // Return |msg| because it could be a framing error.
  }

  const uint8_t* msg_end = msg.start + msg.len;
  PERFETTO_CHECK(msg_end > &buf[rd_] && msg_end <= &buf[wr_]);
  auto msg_outer_len = static_cast<size_t>(msg_end - &buf[rd_]);
  rd_ += msg_outer_len;
  return msg;
}

ProtoRingBuffer::ProtoRingBuffer() = default;
ProtoRingBuffer::~ProtoRingBuffer() = default;

ProtoRingBuffer::Message ProtoRingBuffer::TryReadMessage(const uint8_t* start,
                                                         const uint8_t* end) {
  return TryReadProtoMessage(start, end);
}

}  // namespace protozero
