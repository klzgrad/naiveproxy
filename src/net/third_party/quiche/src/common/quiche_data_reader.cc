// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/common/quiche_data_reader.h"

#include <cstring>

#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quiche {

QuicheDataReader::QuicheDataReader(quiche::QuicheStringPiece data)
    : QuicheDataReader(data.data(), data.length(), quiche::NETWORK_BYTE_ORDER) {
}

QuicheDataReader::QuicheDataReader(const char* data, const size_t len)
    : QuicheDataReader(data, len, quiche::NETWORK_BYTE_ORDER) {}

QuicheDataReader::QuicheDataReader(const char* data,
                                   const size_t len,
                                   quiche::Endianness endianness)
    : data_(data), len_(len), pos_(0), endianness_(endianness) {}

bool QuicheDataReader::ReadUInt8(uint8_t* result) {
  return ReadBytes(result, sizeof(*result));
}

bool QuicheDataReader::ReadUInt16(uint16_t* result) {
  if (!ReadBytes(result, sizeof(*result))) {
    return false;
  }
  if (endianness_ == quiche::NETWORK_BYTE_ORDER) {
    *result = quiche::QuicheEndian::NetToHost16(*result);
  }
  return true;
}

bool QuicheDataReader::ReadUInt32(uint32_t* result) {
  if (!ReadBytes(result, sizeof(*result))) {
    return false;
  }
  if (endianness_ == quiche::NETWORK_BYTE_ORDER) {
    *result = quiche::QuicheEndian::NetToHost32(*result);
  }
  return true;
}

bool QuicheDataReader::ReadUInt64(uint64_t* result) {
  if (!ReadBytes(result, sizeof(*result))) {
    return false;
  }
  if (endianness_ == quiche::NETWORK_BYTE_ORDER) {
    *result = quiche::QuicheEndian::NetToHost64(*result);
  }
  return true;
}

bool QuicheDataReader::ReadBytesToUInt64(size_t num_bytes, uint64_t* result) {
  *result = 0u;
  if (num_bytes > sizeof(*result)) {
    return false;
  }
  if (endianness_ == quiche::HOST_BYTE_ORDER) {
    return ReadBytes(result, num_bytes);
  }

  if (!ReadBytes(reinterpret_cast<char*>(result) + sizeof(*result) - num_bytes,
                 num_bytes)) {
    return false;
  }
  *result = quiche::QuicheEndian::NetToHost64(*result);
  return true;
}

bool QuicheDataReader::ReadStringPiece16(quiche::QuicheStringPiece* result) {
  // Read resultant length.
  uint16_t result_len;
  if (!ReadUInt16(&result_len)) {
    // OnFailure() already called.
    return false;
  }

  return ReadStringPiece(result, result_len);
}

bool QuicheDataReader::ReadStringPiece8(quiche::QuicheStringPiece* result) {
  // Read resultant length.
  uint8_t result_len;
  if (!ReadUInt8(&result_len)) {
    // OnFailure() already called.
    return false;
  }

  return ReadStringPiece(result, result_len);
}

bool QuicheDataReader::ReadStringPiece(quiche::QuicheStringPiece* result,
                                       size_t size) {
  // Make sure that we have enough data to read.
  if (!CanRead(size)) {
    OnFailure();
    return false;
  }

  // Set result.
  *result = quiche::QuicheStringPiece(data_ + pos_, size);

  // Iterate.
  pos_ += size;

  return true;
}

bool QuicheDataReader::ReadTag(uint32_t* tag) {
  return ReadBytes(tag, sizeof(*tag));
}

quiche::QuicheStringPiece QuicheDataReader::ReadRemainingPayload() {
  quiche::QuicheStringPiece payload = PeekRemainingPayload();
  pos_ = len_;
  return payload;
}

quiche::QuicheStringPiece QuicheDataReader::PeekRemainingPayload() const {
  return quiche::QuicheStringPiece(data_ + pos_, len_ - pos_);
}

quiche::QuicheStringPiece QuicheDataReader::FullPayload() const {
  return quiche::QuicheStringPiece(data_, len_);
}

quiche::QuicheStringPiece QuicheDataReader::PreviouslyReadPayload() const {
  return quiche::QuicheStringPiece(data_, pos_);
}

bool QuicheDataReader::ReadBytes(void* result, size_t size) {
  // Make sure that we have enough data to read.
  if (!CanRead(size)) {
    OnFailure();
    return false;
  }

  // Read into result.
  memcpy(result, data_ + pos_, size);

  // Iterate.
  pos_ += size;

  return true;
}

bool QuicheDataReader::Seek(size_t size) {
  if (!CanRead(size)) {
    OnFailure();
    return false;
  }
  pos_ += size;
  return true;
}

bool QuicheDataReader::IsDoneReading() const {
  return len_ == pos_;
}

size_t QuicheDataReader::BytesRemaining() const {
  return len_ - pos_;
}

bool QuicheDataReader::TruncateRemaining(size_t truncation_length) {
  if (truncation_length > BytesRemaining()) {
    return false;
  }
  len_ = pos_ + truncation_length;
  return true;
}

bool QuicheDataReader::CanRead(size_t bytes) const {
  return bytes <= (len_ - pos_);
}

void QuicheDataReader::OnFailure() {
  // Set our iterator to the end of the buffer so that further reads fail
  // immediately.
  pos_ = len_;
}

uint8_t QuicheDataReader::PeekByte() const {
  if (pos_ >= len_) {
    QUICHE_LOG(FATAL)
        << "Reading is done, cannot peek next byte. Tried to read pos = "
        << pos_ << " buffer length = " << len_;
    return 0;
  }
  return data_[pos_];
}

std::string QuicheDataReader::DebugString() const {
  return quiche::QuicheStrCat(" { length: ", len_, ", position: ", pos_, " }");
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quiche
