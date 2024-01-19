// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/spdy/core/spdy_frame_builder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/spdy/core/spdy_bitmasks.h"
#include "quiche/spdy/core/spdy_protocol.h"
#include "quiche/spdy/core/zero_copy_output_buffer.h"

namespace spdy {

SpdyFrameBuilder::SpdyFrameBuilder(size_t size)
    : buffer_(new char[size]), capacity_(size), length_(0), offset_(0) {}

SpdyFrameBuilder::SpdyFrameBuilder(size_t size, ZeroCopyOutputBuffer* output)
    : buffer_(output == nullptr ? new char[size] : nullptr),
      output_(output),
      capacity_(size),
      length_(0),
      offset_(0) {}

SpdyFrameBuilder::~SpdyFrameBuilder() = default;

char* SpdyFrameBuilder::GetWritableBuffer(size_t length) {
  if (!CanWrite(length)) {
    return nullptr;
  }
  return buffer_.get() + offset_ + length_;
}

char* SpdyFrameBuilder::GetWritableOutput(size_t length,
                                          size_t* actual_length) {
  char* dest = nullptr;
  int size = 0;

  if (!CanWrite(length)) {
    return nullptr;
  }
  output_->Next(&dest, &size);
  *actual_length = std::min<size_t>(length, size);
  return dest;
}

bool SpdyFrameBuilder::Seek(size_t length) {
  if (!CanWrite(length)) {
    return false;
  }
  if (output_ == nullptr) {
    length_ += length;
  } else {
    output_->AdvanceWritePtr(length);
    length_ += length;
  }
  return true;
}

bool SpdyFrameBuilder::BeginNewFrame(SpdyFrameType type, uint8_t flags,
                                     SpdyStreamId stream_id) {
  uint8_t raw_frame_type = SerializeFrameType(type);
  QUICHE_DCHECK(IsDefinedFrameType(raw_frame_type));
  QUICHE_DCHECK_EQ(0u, stream_id & ~kStreamIdMask);
  bool success = true;
  if (length_ > 0) {
    QUICHE_BUG(spdy_bug_73_1)
        << "SpdyFrameBuilder doesn't have a clean state when BeginNewFrame"
        << "is called. Leftover length_ is " << length_;
    offset_ += length_;
    length_ = 0;
  }

  success &= WriteUInt24(capacity_ - offset_ - kFrameHeaderSize);
  success &= WriteUInt8(raw_frame_type);
  success &= WriteUInt8(flags);
  success &= WriteUInt32(stream_id);
  QUICHE_DCHECK_EQ(kDataFrameMinimumSize, length_);
  return success;
}

bool SpdyFrameBuilder::BeginNewFrame(SpdyFrameType type, uint8_t flags,
                                     SpdyStreamId stream_id, size_t length) {
  uint8_t raw_frame_type = SerializeFrameType(type);
  QUICHE_DCHECK(IsDefinedFrameType(raw_frame_type));
  QUICHE_DCHECK_EQ(0u, stream_id & ~kStreamIdMask);
  QUICHE_BUG_IF(spdy_bug_73_2, length > kSpdyMaxFrameSizeLimit)
      << "Frame length  " << length << " is longer than frame size limit.";
  return BeginNewFrameInternal(raw_frame_type, flags, stream_id, length);
}

bool SpdyFrameBuilder::BeginNewUncheckedFrame(uint8_t raw_frame_type,
                                              uint8_t flags,
                                              SpdyStreamId stream_id,
                                              size_t length) {
  return BeginNewFrameInternal(raw_frame_type, flags, stream_id, length);
}

bool SpdyFrameBuilder::BeginNewFrameInternal(uint8_t raw_frame_type,
                                             uint8_t flags,
                                             SpdyStreamId stream_id,
                                             size_t length) {
  QUICHE_DCHECK_EQ(length, length & kLengthMask);
  bool success = true;

  offset_ += length_;
  length_ = 0;

  success &= WriteUInt24(length);
  success &= WriteUInt8(raw_frame_type);
  success &= WriteUInt8(flags);
  success &= WriteUInt32(stream_id);
  QUICHE_DCHECK_EQ(kDataFrameMinimumSize, length_);
  return success;
}

bool SpdyFrameBuilder::WriteStringPiece32(const absl::string_view value) {
  if (!WriteUInt32(value.size())) {
    return false;
  }

  return WriteBytes(value.data(), value.size());
}

bool SpdyFrameBuilder::WriteBytes(const void* data, uint32_t data_len) {
  if (!CanWrite(data_len)) {
    return false;
  }

  if (output_ == nullptr) {
    char* dest = GetWritableBuffer(data_len);
    memcpy(dest, data, data_len);
    Seek(data_len);
  } else {
    char* dest = nullptr;
    size_t size = 0;
    size_t total_written = 0;
    const char* data_ptr = reinterpret_cast<const char*>(data);
    while (data_len > 0) {
      dest = GetWritableOutput(data_len, &size);
      if (dest == nullptr || size == 0) {
        // Unable to make progress.
        return false;
      }
      uint32_t to_copy = std::min<uint32_t>(data_len, size);
      const char* src = data_ptr + total_written;
      memcpy(dest, src, to_copy);
      Seek(to_copy);
      data_len -= to_copy;
      total_written += to_copy;
    }
  }
  return true;
}

bool SpdyFrameBuilder::CanWrite(size_t length) const {
  if (length > kLengthMask) {
    QUICHE_DCHECK(false);
    return false;
  }

  if (output_ == nullptr) {
    if (offset_ + length_ + length > capacity_) {
      QUICHE_DLOG(FATAL) << "Requested: " << length
                         << " capacity: " << capacity_
                         << " used: " << offset_ + length_;
      return false;
    }
  } else {
    if (length > output_->BytesFree()) {
      return false;
    }
  }

  return true;
}

}  // namespace spdy
