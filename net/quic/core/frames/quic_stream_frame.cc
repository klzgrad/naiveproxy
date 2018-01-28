// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/frames/quic_stream_frame.h"

#include "net/quic/platform/api/quic_logging.h"

namespace net {

void StreamBufferDeleter::operator()(char* buffer) const {
  if (allocator_ != nullptr && buffer != nullptr) {
    allocator_->Delete(buffer);
  }
}

UniqueStreamBuffer NewStreamBuffer(QuicBufferAllocator* allocator,
                                   size_t size) {
  return UniqueStreamBuffer(allocator->New(size),
                            StreamBufferDeleter(allocator));
}

QuicStreamFrame::QuicStreamFrame()
    : QuicStreamFrame(0, false, 0, nullptr, 0, nullptr) {}

QuicStreamFrame::QuicStreamFrame(QuicStreamId stream_id,
                                 bool fin,
                                 QuicStreamOffset offset,
                                 QuicStringPiece data)
    : QuicStreamFrame(stream_id,
                      fin,
                      offset,
                      data.data(),
                      data.length(),
                      nullptr) {}

QuicStreamFrame::QuicStreamFrame(QuicStreamId stream_id,
                                 bool fin,
                                 QuicStreamOffset offset,
                                 QuicPacketLength data_length,
                                 UniqueStreamBuffer buffer)
    : QuicStreamFrame(stream_id,
                      fin,
                      offset,
                      nullptr,
                      data_length,
                      std::move(buffer)) {
  DCHECK(this->buffer != nullptr);
  DCHECK_EQ(data_buffer, this->buffer.get());
}

QuicStreamFrame::QuicStreamFrame(QuicStreamId stream_id,
                                 bool fin,
                                 QuicStreamOffset offset,
                                 QuicPacketLength data_length)
    : QuicStreamFrame(stream_id, fin, offset, nullptr, data_length, nullptr) {}

QuicStreamFrame::QuicStreamFrame(QuicStreamId stream_id,
                                 bool fin,
                                 QuicStreamOffset offset,
                                 const char* data_buffer,
                                 QuicPacketLength data_length,
                                 UniqueStreamBuffer buffer)
    : stream_id(stream_id),
      fin(fin),
      data_length(data_length),
      data_buffer(data_buffer),
      offset(offset),
      buffer(std::move(buffer)) {
  if (this->buffer != nullptr) {
    DCHECK(data_buffer == nullptr);
    this->data_buffer = this->buffer.get();
  }
}

QuicStreamFrame::~QuicStreamFrame() {}

std::ostream& operator<<(std::ostream& os,
                         const QuicStreamFrame& stream_frame) {
  os << "{ stream_id: " << stream_frame.stream_id
     << ", fin: " << stream_frame.fin << ", offset: " << stream_frame.offset
     << ", length: " << stream_frame.data_length << " }\n";
  return os;
}

}  // namespace net
