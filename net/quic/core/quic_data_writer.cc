// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_data_writer.h"

#include <algorithm>
#include <limits>

#include "net/quic/core/quic_utils.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"

namespace net {

#define ENDPOINT \
  (perspective_ == Perspective::IS_SERVER ? "Server: " : "Client: ")

QuicDataWriter::QuicDataWriter(size_t size, char* buffer, Endianness endianness)
    : buffer_(buffer), capacity_(size), length_(0), endianness_(endianness) {}

QuicDataWriter::~QuicDataWriter() {}

char* QuicDataWriter::data() {
  return buffer_;
}

bool QuicDataWriter::WriteUInt8(uint8_t value) {
  return WriteBytes(&value, sizeof(value));
}

bool QuicDataWriter::WriteUInt16(uint16_t value) {
  if (endianness_ == NETWORK_BYTE_ORDER) {
    value = QuicEndian::HostToNet16(value);
  }
  return WriteBytes(&value, sizeof(value));
}

bool QuicDataWriter::WriteUInt32(uint32_t value) {
  if (endianness_ == NETWORK_BYTE_ORDER) {
    value = QuicEndian::HostToNet32(value);
  }
  return WriteBytes(&value, sizeof(value));
}

bool QuicDataWriter::WriteUInt64(uint64_t value) {
  if (endianness_ == NETWORK_BYTE_ORDER) {
    value = QuicEndian::HostToNet64(value);
  }
  return WriteBytes(&value, sizeof(value));
}

bool QuicDataWriter::WriteUInt8AtOffset(uint8_t value, size_t offset) {
  if (offset > length_) {
    return false;
  }
  size_t old_length = length_;
  length_ = offset;
  bool result = WriteBytes(&value, sizeof(value));
  length_ = old_length;
  return result;
}

bool QuicDataWriter::WriteBytesToUInt64(size_t num_bytes, uint64_t value) {
  if (num_bytes > sizeof(value)) {
    return false;
  }
  if (endianness_ == HOST_BYTE_ORDER) {
    return WriteBytes(&value, num_bytes);
  }

  value = QuicEndian::HostToNet64(value);
  return WriteBytes(reinterpret_cast<char*>(&value) + sizeof(value) - num_bytes,
                    num_bytes);
}

bool QuicDataWriter::WriteUFloat16(uint64_t value) {
  uint16_t result;
  if (value < (UINT64_C(1) << kUFloat16MantissaEffectiveBits)) {
    // Fast path: either the value is denormalized, or has exponent zero.
    // Both cases are represented by the value itself.
    result = static_cast<uint16_t>(value);
  } else if (value >= kUFloat16MaxValue) {
    // Value is out of range; clamp it to the maximum representable.
    result = std::numeric_limits<uint16_t>::max();
  } else {
    // The highest bit is between position 13 and 42 (zero-based), which
    // corresponds to exponent 1-30. In the output, mantissa is from 0 to 10,
    // hidden bit is 11 and exponent is 11 to 15. Shift the highest bit to 11
    // and count the shifts.
    uint16_t exponent = 0;
    for (uint16_t offset = 16; offset > 0; offset /= 2) {
      // Right-shift the value until the highest bit is in position 11.
      // For offset of 16, 8, 4, 2 and 1 (binary search over 1-30),
      // shift if the bit is at or above 11 + offset.
      if (value >= (UINT64_C(1) << (kUFloat16MantissaBits + offset))) {
        exponent += offset;
        value >>= offset;
      }
    }

    DCHECK_GE(exponent, 1);
    DCHECK_LE(exponent, kUFloat16MaxExponent);
    DCHECK_GE(value, UINT64_C(1) << kUFloat16MantissaBits);
    DCHECK_LT(value, UINT64_C(1) << kUFloat16MantissaEffectiveBits);

    // Hidden bit (position 11) is set. We should remove it and increment the
    // exponent. Equivalently, we just add it to the exponent.
    // This hides the bit.
    result = static_cast<uint16_t>(value + (exponent << kUFloat16MantissaBits));
  }

  if (endianness_ == NETWORK_BYTE_ORDER) {
    result = QuicEndian::HostToNet16(result);
  }
  return WriteBytes(&result, sizeof(result));
}

bool QuicDataWriter::WriteStringPiece16(QuicStringPiece val) {
  if (val.size() > std::numeric_limits<uint16_t>::max()) {
    return false;
  }
  if (!WriteUInt16(static_cast<uint16_t>(val.size()))) {
    return false;
  }
  return WriteBytes(val.data(), val.size());
}

char* QuicDataWriter::BeginWrite(size_t length) {
  if (length_ > capacity_) {
    return nullptr;
  }

  if (capacity_ - length_ < length) {
    return nullptr;
  }

#ifdef ARCH_CPU_64_BITS
  DCHECK_LE(length, std::numeric_limits<uint32_t>::max());
#endif

  return buffer_ + length_;
}

bool QuicDataWriter::WriteBytes(const void* data, size_t data_len) {
  char* dest = BeginWrite(data_len);
  if (!dest) {
    return false;
  }

  memcpy(dest, data, data_len);

  length_ += data_len;
  return true;
}

bool QuicDataWriter::WriteRepeatedByte(uint8_t byte, size_t count) {
  char* dest = BeginWrite(count);
  if (!dest) {
    return false;
  }

  memset(dest, byte, count);

  length_ += count;
  return true;
}

void QuicDataWriter::WritePadding() {
  DCHECK_LE(length_, capacity_);
  if (length_ > capacity_) {
    return;
  }
  memset(buffer_ + length_, 0x00, capacity_ - length_);
  length_ = capacity_;
}

bool QuicDataWriter::WritePaddingBytes(size_t count) {
  return WriteRepeatedByte(0x00, count);
}

bool QuicDataWriter::WriteConnectionId(uint64_t connection_id) {
  connection_id = QuicEndian::HostToNet64(connection_id);

  return WriteBytes(&connection_id, sizeof(connection_id));
}

bool QuicDataWriter::WriteTag(uint32_t tag) {
  return WriteBytes(&tag, sizeof(tag));
}

}  // namespace net
