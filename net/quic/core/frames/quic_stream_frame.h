// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_FRAMES_QUIC_STREAM_FRAME_H_
#define NET_QUIC_CORE_FRAMES_QUIC_STREAM_FRAME_H_

#include <memory>
#include <ostream>

#include "net/quic/core/quic_buffer_allocator.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

// Deleter for stream buffers. Copyable to support platforms where the deleter
// of a unique_ptr must be copyable. Otherwise it would be nice for this to be
// move-only.
class QUIC_EXPORT_PRIVATE StreamBufferDeleter {
 public:
  StreamBufferDeleter() : allocator_(nullptr) {}
  explicit StreamBufferDeleter(QuicBufferAllocator* allocator)
      : allocator_(allocator) {}

  // Deletes |buffer| using |allocator_|.
  void operator()(char* buffer) const;

 private:
  // Not owned; must be valid so long as the buffer stored in the unique_ptr
  // that owns |this| is valid.
  QuicBufferAllocator* allocator_;
};

using UniqueStreamBuffer = std::unique_ptr<char[], StreamBufferDeleter>;

// Allocates memory of size |size| using |allocator| for a QUIC stream buffer.
QUIC_EXPORT_PRIVATE UniqueStreamBuffer
NewStreamBuffer(QuicBufferAllocator* allocator, size_t size);

struct QUIC_EXPORT_PRIVATE QuicStreamFrame {
  QuicStreamFrame();
  QuicStreamFrame(QuicStreamId stream_id,
                  bool fin,
                  QuicStreamOffset offset,
                  QuicStringPiece data);
  QuicStreamFrame(QuicStreamId stream_id,
                  bool fin,
                  QuicStreamOffset offset,
                  QuicPacketLength data_length,
                  UniqueStreamBuffer buffer);
  QuicStreamFrame(QuicStreamId stream_id,
                  bool fin,
                  QuicStreamOffset offset,
                  QuicPacketLength data_length);
  ~QuicStreamFrame();

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                                      const QuicStreamFrame& s);

  QuicStreamId stream_id;
  bool fin;
  QuicPacketLength data_length;
  const char* data_buffer;
  QuicStreamOffset offset;  // Location of this data in the stream.
  // TODO(fayang): (1) Remove buffer from QuicStreamFrame; (2) remove the
  // constructor uses UniqueStreamBuffer and (3) Move definition of
  // UniqueStreamBuffer to QuicStreamSendBuffer. nullptr when the
  // QuicStreamFrame is received, and non-null when sent.
  UniqueStreamBuffer buffer;

 private:
  QuicStreamFrame(QuicStreamId stream_id,
                  bool fin,
                  QuicStreamOffset offset,
                  const char* data_buffer,
                  QuicPacketLength data_length,
                  UniqueStreamBuffer buffer);

  DISALLOW_COPY_AND_ASSIGN(QuicStreamFrame);
};
static_assert(sizeof(QuicStreamFrame) <= 64,
              "Keep the QuicStreamFrame size to a cacheline.");

}  // namespace net

#endif  // NET_QUIC_CORE_FRAMES_QUIC_STREAM_FRAME_H_
