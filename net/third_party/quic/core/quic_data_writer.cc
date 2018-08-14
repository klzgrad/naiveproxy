// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_data_writer.h"

#include <algorithm>
#include <limits>

#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_str_cat.h"

namespace quic {

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

bool QuicDataWriter::WriteStringPiece(QuicStringPiece val) {
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

// Converts a uint64_t into an IETF/Quic formatted Variable Length
// Integer. IETF Variable Length Integers have 62 significant bits, so
// the value to write must be in the range of 0..(2^62)-1.
//
// Performance notes
//
// Measurements and experiments showed that unrolling the four cases
// like this and dereferencing next_ as we do (*(next_+n)) gains about
// 10% over making a loop and dereferencing it as *(next_++)
//
// Using a register for next didn't help.
//
// Branches are ordered to increase the likelihood of the first being
// taken.
//
// Low-level optimization is useful here because this function will be
// called frequently, leading to outsize benefits.
bool QuicDataWriter::WriteVarInt62(uint64_t value) {
  DCHECK_EQ(endianness_, NETWORK_BYTE_ORDER);

  size_t remaining = capacity_ - length_;
  char* next = buffer_ + length_;

  if ((value & kVarInt62ErrorMask) == 0) {
    // We know the high 2 bits are 0 so |value| is legal.
    // We can do the encoding.
    if ((value & kVarInt62Mask8Bytes) != 0) {
      // Someplace in the high-4 bytes is a 1-bit. Do an 8-byte
      // encoding.
      if (remaining >= 8) {
        *(next + 0) = ((value >> 56) & 0x3f) + 0xc0;
        *(next + 1) = (value >> 48) & 0xff;
        *(next + 2) = (value >> 40) & 0xff;
        *(next + 3) = (value >> 32) & 0xff;
        *(next + 4) = (value >> 24) & 0xff;
        *(next + 5) = (value >> 16) & 0xff;
        *(next + 6) = (value >> 8) & 0xff;
        *(next + 7) = value & 0xff;
        length_ += 8;
        return true;
      }
      return false;
    }
    // The high-order-4 bytes are all 0, check for a 1, 2, or 4-byte
    // encoding
    if ((value & kVarInt62Mask4Bytes) != 0) {
      // The encoding will not fit into 2 bytes, Do a 4-byte
      // encoding.
      if (remaining >= 4) {
        *(next + 0) = ((value >> 24) & 0x3f) + 0x80;
        *(next + 1) = (value >> 16) & 0xff;
        *(next + 2) = (value >> 8) & 0xff;
        *(next + 3) = value & 0xff;
        length_ += 4;
        return true;
      }
      return false;
    }
    // The high-order bits are all 0. Check to see if the number
    // can be encoded as one or two bytes. One byte encoding has
    // only 6 significant bits (bits 0xffffffff ffffffc0 are all 0).
    // Two byte encoding has more than 6, but 14 or less significant
    // bits (bits 0xffffffff ffffc000 are 0 and 0x00000000 00003fc0
    // are not 0)
    if ((value & kVarInt62Mask2Bytes) != 0) {
      // Do 2-byte encoding
      if (remaining >= 2) {
        *(next + 0) = ((value >> 8) & 0x3f) + 0x40;
        *(next + 1) = (value)&0xff;
        length_ += 2;
        return true;
      }
      return false;
    }
    if (remaining >= 1) {
      // Do 1-byte encoding
      *next = (value & 0x3f);
      length_ += 1;
      return true;
    }
    return false;
  }
  // Can not encode, high 2 bits not 0
  return false;
}

// static
int QuicDataWriter::GetVarInt62Len(uint64_t value) {
  if ((value & kVarInt62ErrorMask) != 0) {
    QUIC_BUG << "Attempted to encode a value, " << value
             << ", that is too big for VarInt62";
    return 0;
  }
  if ((value & kVarInt62Mask8Bytes) != 0) {
    return 8;
  }
  if ((value & kVarInt62Mask4Bytes) != 0) {
    return 4;
  }
  if ((value & kVarInt62Mask2Bytes) != 0) {
    return 2;
  }
  return 1;
}

bool QuicDataWriter::WriteStringPieceVarInt62(
    const QuicStringPiece& string_piece) {
  if (!WriteVarInt62(string_piece.size())) {
    return false;
  }
  if (!string_piece.empty()) {
    if (!WriteBytes(string_piece.data(), string_piece.size())) {
      return false;
    }
  }
  return true;
}

QuicString QuicDataWriter::DebugString() const {
  return QuicStrCat(" { capacity: ", capacity_, ", length: ", length_, " }");
}

}  // namespace quic
