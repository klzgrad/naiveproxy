// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/common/quiche_data_writer.h"

#include <algorithm>
#include <limits>

#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quiche {

QuicheDataWriter::QuicheDataWriter(size_t size, char* buffer)
    : QuicheDataWriter(size, buffer, quiche::NETWORK_BYTE_ORDER) {}

QuicheDataWriter::QuicheDataWriter(size_t size,
                                   char* buffer,
                                   quiche::Endianness endianness)
    : buffer_(buffer), capacity_(size), length_(0), endianness_(endianness) {}

QuicheDataWriter::~QuicheDataWriter() {}

char* QuicheDataWriter::data() {
  return buffer_;
}

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

bool QuicheDataWriter::WriteStringPiece16(quiche::QuicheStringPiece val) {
  if (val.size() > std::numeric_limits<uint16_t>::max()) {
    return false;
  }
  if (!WriteUInt16(static_cast<uint16_t>(val.size()))) {
    return false;
  }
  return WriteBytes(val.data(), val.size());
}

bool QuicheDataWriter::WriteStringPiece(quiche::QuicheStringPiece val) {
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
  DCHECK_LE(length, std::numeric_limits<uint32_t>::max());
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
  DCHECK_LE(length_, capacity_);
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

bool QuicheDataWriter::Seek(size_t length) {
  if (!BeginWrite(length)) {
    return false;
  }
  length_ += length;
  return true;
}

std::string QuicheDataWriter::DebugString() const {
  return quiche::QuicheStrCat(" { capacity: ", capacity_, ", length: ", length_,
                              " }");
}

}  // namespace quiche
