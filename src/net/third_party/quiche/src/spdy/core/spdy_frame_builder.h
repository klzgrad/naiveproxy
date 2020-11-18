// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_SPDY_FRAME_BUILDER_H_
#define QUICHE_SPDY_CORE_SPDY_FRAME_BUILDER_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"
#include "net/third_party/quiche/src/spdy/core/zero_copy_output_buffer.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_bug_tracker.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_endianness_util.h"

namespace spdy {

namespace test {
class SpdyFrameBuilderPeer;
}  // namespace test

// This class provides facilities for basic binary value packing
// into Spdy frames.
//
// The SpdyFrameBuilder supports appending primitive values (int, string, etc)
// to a frame instance.  The SpdyFrameBuilder grows its internal memory buffer
// dynamically to hold the sequence of primitive values.   The internal memory
// buffer is exposed as the "data" of the SpdyFrameBuilder.
class QUICHE_EXPORT_PRIVATE SpdyFrameBuilder {
 public:
  // Initializes a SpdyFrameBuilder with a buffer of given size
  explicit SpdyFrameBuilder(size_t size);
  // Doesn't take ownership of output.
  SpdyFrameBuilder(size_t size, ZeroCopyOutputBuffer* output);

  ~SpdyFrameBuilder();

  // Returns the total size of the SpdyFrameBuilder's data, which may include
  // multiple frames.
  size_t length() const { return offset_ + length_; }

  // Seeks forward by the given number of bytes. Useful in conjunction with
  // GetWriteableBuffer() above.
  bool Seek(size_t length);

  // Populates this frame with a HTTP2 frame prefix using length information
  // from |capacity_|. The given type must be a control frame type.
  bool BeginNewFrame(SpdyFrameType type, uint8_t flags, SpdyStreamId stream_id);

  // Populates this frame with a HTTP2 frame prefix with type and length
  // information.  |type| must be a defined frame type.
  bool BeginNewFrame(SpdyFrameType type,
                     uint8_t flags,
                     SpdyStreamId stream_id,
                     size_t length);

  // Populates this frame with a HTTP2 frame prefix with type and length
  // information.  |raw_frame_type| may be a defined or undefined frame type.
  bool BeginNewUncheckedFrame(uint8_t raw_frame_type,
                              uint8_t flags,
                              SpdyStreamId stream_id,
                              size_t length);

  // Takes the buffer from the SpdyFrameBuilder.
  SpdySerializedFrame take() {
    SPDY_BUG_IF(output_ != nullptr) << "ZeroCopyOutputBuffer is used to build "
                                    << "frames. take() shouldn't be called";
    SPDY_BUG_IF(kMaxFrameSizeLimit < length_)
        << "Frame length " << length_
        << " is longer than the maximum possible allowed length.";
    SpdySerializedFrame rv(buffer_.release(), length(), true);
    capacity_ = 0;
    length_ = 0;
    offset_ = 0;
    return rv;
  }

  // Methods for adding to the payload.  These values are appended to the end
  // of the SpdyFrameBuilder payload. Note - binary integers are converted from
  // host to network form.
  bool WriteUInt8(uint8_t value) { return WriteBytes(&value, sizeof(value)); }
  bool WriteUInt16(uint16_t value) {
    value = SpdyHostToNet16(value);
    return WriteBytes(&value, sizeof(value));
  }
  bool WriteUInt24(uint32_t value) {
    value = SpdyHostToNet32(value);
    return WriteBytes(reinterpret_cast<char*>(&value) + 1, sizeof(value) - 1);
  }
  bool WriteUInt32(uint32_t value) {
    value = SpdyHostToNet32(value);
    return WriteBytes(&value, sizeof(value));
  }
  bool WriteUInt64(uint64_t value) {
    uint32_t upper = SpdyHostToNet32(static_cast<uint32_t>(value >> 32));
    uint32_t lower = SpdyHostToNet32(static_cast<uint32_t>(value));
    return (WriteBytes(&upper, sizeof(upper)) &&
            WriteBytes(&lower, sizeof(lower)));
  }
  bool WriteStringPiece32(const quiche::QuicheStringPiece value);
  bool WriteBytes(const void* data, uint32_t data_len);

 private:
  friend class test::SpdyFrameBuilderPeer;

  // Populates this frame with a HTTP2 frame prefix with type and length
  // information.
  bool BeginNewFrameInternal(uint8_t raw_frame_type,
                             uint8_t flags,
                             SpdyStreamId stream_id,
                             size_t length);

  // Returns a writeable buffer of given size in bytes, to be appended to the
  // currently written frame. Does bounds checking on length but does not
  // increment the underlying iterator. To do so, consumers should subsequently
  // call Seek().
  // In general, consumers should use Write*() calls instead of this.
  // Returns NULL on failure.
  char* GetWritableBuffer(size_t length);
  char* GetWritableOutput(size_t desired_length, size_t* actual_length);

  // Checks to make sure that there is an appropriate amount of space for a
  // write of given size, in bytes.
  bool CanWrite(size_t length) const;

  // A buffer to be created whenever a new frame needs to be written. Used only
  // if |output_| is nullptr.
  std::unique_ptr<char[]> buffer_;
  // A pre-allocated buffer. If not-null, serialized frame data is written to
  // this buffer.
  ZeroCopyOutputBuffer* output_ = nullptr;  // Does not own.

  size_t capacity_;  // Allocation size of payload, set by constructor.
  size_t length_;    // Length of the latest frame in the buffer.
  size_t offset_;    // Position at which the latest frame begins.
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_SPDY_FRAME_BUILDER_H_
