// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/quiche_data_writer.h"

#include <algorithm>
#include <limits>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/quiche_endian.h"

namespace quiche {

QuicheDataWriter::QuicheDataWriter(size_t size, char* buffer)
    : QuicheDataWriter(size, buffer, quiche::NETWORK_BYTE_ORDER) {}

QuicheDataWriter::QuicheDataWriter(size_t size, char* buffer,
                                   quiche::Endianness endianness)
    : buffer_(buffer), capacity_(size), length_(0), endianness_(endianness) {}

QuicheDataWriter::~QuicheDataWriter() {}

char* QuicheDataWriter::data() { return buffer_; }

bool QuicheDataWriter::WriteUInt8(uint8_t value) {
  return WriteBytes(&value, sizeof(value));
}

bool QuicheDataWriter::WriteUInt16(uint16_t value) {
  if (endianness_ == quiche::NETWORK_BYTE_ORDER) {
    value = quiche::QuicheEndian::HostToNet16(value);
  }
  return WriteBytes(&value, sizeof(value));
}

bool QuicheDataWriter::WriteUInt32(uint32_t value) {
  if (endianness_ == quiche::NETWORK_BYTE_ORDER) {
    value = quiche::QuicheEndian::HostToNet32(value);
  }
  return WriteBytes(&value, sizeof(value));
}

bool QuicheDataWriter::WriteUInt64(uint64_t value) {
  if (endianness_ == quiche::NETWORK_BYTE_ORDER) {
    value = quiche::QuicheEndian::HostToNet64(value);
  }
  return WriteBytes(&value, sizeof(value));
}

bool QuicheDataWriter::WriteBytesToUInt64(size_t num_bytes, uint64_t value) {
  if (num_bytes > sizeof(value)) {
    return false;
  }
  if (endianness_ == quiche::HOST_BYTE_ORDER) {
    return WriteBytes(&value, num_bytes);
  }

  value = quiche::QuicheEndian::HostToNet64(value);
  return WriteBytes(reinterpret_cast<char*>(&value) + sizeof(value) - num_bytes,
                    num_bytes);
}

bool QuicheDataWriter::WriteStringPiece16(absl::string_view val) {
  if (val.size() > std::numeric_limits<uint16_t>::max()) {
    return false;
  }
  if (!WriteUInt16(static_cast<uint16_t>(val.size()))) {
    return false;
  }
  return WriteBytes(val.data(), val.size());
}

bool QuicheDataWriter::WriteStringPiece(absl::string_view val) {
  return WriteBytes(val.data(), val.size());
}

char* QuicheDataWriter::BeginWrite(size_t length) {
  if (length_ > capacity_) {
    return nullptr;
  }

  if (capacity_ - length_ < length) {
    return nullptr;
  }

#ifdef ARCH_CPU_64_BITS
  QUICHE_DCHECK_LE(length, std::numeric_limits<uint32_t>::max());
#endif

  return buffer_ + length_;
}

bool QuicheDataWriter::WriteBytes(const void* data, size_t data_len) {
  char* dest = BeginWrite(data_len);
  if (!dest) {
    return false;
  }

  memcpy(dest, data, data_len);

  length_ += data_len;
  return true;
}

bool QuicheDataWriter::WriteRepeatedByte(uint8_t byte, size_t count) {
  char* dest = BeginWrite(count);
  if (!dest) {
    return false;
  }

  memset(dest, byte, count);

  length_ += count;
  return true;
}

void QuicheDataWriter::WritePadding() {
  QUICHE_DCHECK_LE(length_, capacity_);
  if (length_ > capacity_) {
    return;
  }
  memset(buffer_ + length_, 0x00, capacity_ - length_);
  length_ = capacity_;
}

bool QuicheDataWriter::WritePaddingBytes(size_t count) {
  return WriteRepeatedByte(0x00, count);
}

bool QuicheDataWriter::WriteTag(uint32_t tag) {
  return WriteBytes(&tag, sizeof(tag));
}

// Converts a uint64_t into a 62-bit RFC 9000 Variable Length Integer.
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
bool QuicheDataWriter::WriteVarInt62(uint64_t value) {
  QUICHE_DCHECK_EQ(endianness(), quiche::NETWORK_BYTE_ORDER);

  size_t remaining_bytes = remaining();
  char* next = buffer() + length();

  if ((value & kVarInt62ErrorMask) == 0) {
    // We know the high 2 bits are 0 so |value| is legal.
    // We can do the encoding.
    if ((value & kVarInt62Mask8Bytes) != 0) {
      // Someplace in the high-4 bytes is a 1-bit. Do an 8-byte
      // encoding.
      if (remaining_bytes >= 8) {
        *(next + 0) = ((value >> 56) & 0x3f) + 0xc0;
        *(next + 1) = (value >> 48) & 0xff;
        *(next + 2) = (value >> 40) & 0xff;
        *(next + 3) = (value >> 32) & 0xff;
        *(next + 4) = (value >> 24) & 0xff;
        *(next + 5) = (value >> 16) & 0xff;
        *(next + 6) = (value >> 8) & 0xff;
        *(next + 7) = value & 0xff;
        IncreaseLength(8);
        return true;
      }
      return false;
    }
    // The high-order-4 bytes are all 0, check for a 1, 2, or 4-byte
    // encoding
    if ((value & kVarInt62Mask4Bytes) != 0) {
      // The encoding will not fit into 2 bytes, Do a 4-byte
      // encoding.
      if (remaining_bytes >= 4) {
        *(next + 0) = ((value >> 24) & 0x3f) + 0x80;
        *(next + 1) = (value >> 16) & 0xff;
        *(next + 2) = (value >> 8) & 0xff;
        *(next + 3) = value & 0xff;
        IncreaseLength(4);
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
      if (remaining_bytes >= 2) {
        *(next + 0) = ((value >> 8) & 0x3f) + 0x40;
        *(next + 1) = (value)&0xff;
        IncreaseLength(2);
        return true;
      }
      return false;
    }
    if (remaining_bytes >= 1) {
      // Do 1-byte encoding
      *next = (value & 0x3f);
      IncreaseLength(1);
      return true;
    }
    return false;
  }
  // Can not encode, high 2 bits not 0
  return false;
}

bool QuicheDataWriter::WriteStringPieceVarInt62(
    const absl::string_view& string_piece) {
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

// static
QuicheVariableLengthIntegerLength QuicheDataWriter::GetVarInt62Len(
    uint64_t value) {
  if ((value & kVarInt62ErrorMask) != 0) {
    QUICHE_BUG(invalid_varint) << "Attempted to encode a value, " << value
                               << ", that is too big for VarInt62";
    return VARIABLE_LENGTH_INTEGER_LENGTH_0;
  }
  if ((value & kVarInt62Mask8Bytes) != 0) {
    return VARIABLE_LENGTH_INTEGER_LENGTH_8;
  }
  if ((value & kVarInt62Mask4Bytes) != 0) {
    return VARIABLE_LENGTH_INTEGER_LENGTH_4;
  }
  if ((value & kVarInt62Mask2Bytes) != 0) {
    return VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }
  return VARIABLE_LENGTH_INTEGER_LENGTH_1;
}

bool QuicheDataWriter::WriteVarInt62WithForcedLength(
    uint64_t value, QuicheVariableLengthIntegerLength write_length) {
  QUICHE_DCHECK_EQ(endianness(), NETWORK_BYTE_ORDER);

  size_t remaining_bytes = remaining();
  if (remaining_bytes < write_length) {
    return false;
  }

  const QuicheVariableLengthIntegerLength min_length = GetVarInt62Len(value);
  if (write_length < min_length) {
    QUICHE_BUG(invalid_varint_forced) << "Cannot write value " << value
                                      << " with write_length " << write_length;
    return false;
  }
  if (write_length == min_length) {
    return WriteVarInt62(value);
  }

  if (write_length == VARIABLE_LENGTH_INTEGER_LENGTH_2) {
    return WriteUInt8(0b01000000) && WriteUInt8(value);
  }
  if (write_length == VARIABLE_LENGTH_INTEGER_LENGTH_4) {
    return WriteUInt8(0b10000000) && WriteUInt8(0) && WriteUInt16(value);
  }
  if (write_length == VARIABLE_LENGTH_INTEGER_LENGTH_8) {
    return WriteUInt8(0b11000000) && WriteUInt8(0) && WriteUInt16(0) &&
           WriteUInt32(value);
  }

  QUICHE_BUG(invalid_write_length)
      << "Invalid write_length " << static_cast<int>(write_length);
  return false;
}

bool QuicheDataWriter::Seek(size_t length) {
  if (!BeginWrite(length)) {
    return false;
  }
  length_ += length;
  return true;
}

std::string QuicheDataWriter::DebugString() const {
  return absl::StrCat(" { capacity: ", capacity_, ", length: ", length_, " }");
}

}  // namespace quiche
