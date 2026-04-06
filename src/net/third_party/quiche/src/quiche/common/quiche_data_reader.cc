// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/quiche_data_reader.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"

namespace quiche {

QuicheDataReader::QuicheDataReader(absl::string_view data)
    : QuicheDataReader(data.data(), data.length(), quiche::NETWORK_BYTE_ORDER) {
}

QuicheDataReader::QuicheDataReader(const char* data, const size_t len)
    : QuicheDataReader(data, len, quiche::NETWORK_BYTE_ORDER) {}

QuicheDataReader::QuicheDataReader(const char* data, const size_t len,
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

bool QuicheDataReader::ReadUInt24(uint32_t* result) {
  if (endianness_ != quiche::NETWORK_BYTE_ORDER) {
    // TODO(b/214573190): Implement and test HOST_BYTE_ORDER case.
    QUICHE_BUG(QuicheDataReader_ReadUInt24_NotImplemented);
    return false;
  }

  *result = 0;
  if (!ReadBytes(reinterpret_cast<char*>(result) + 1, 3u)) {
    return false;
  }
  *result = quiche::QuicheEndian::NetToHost32(*result);
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

bool QuicheDataReader::ReadStringPiece16(absl::string_view* result) {
  // Read resultant length.
  uint16_t result_len;
  if (!ReadUInt16(&result_len)) {
    // OnFailure() already called.
    return false;
  }

  return ReadStringPiece(result, result_len);
}

bool QuicheDataReader::ReadStringPiece8(absl::string_view* result) {
  // Read resultant length.
  uint8_t result_len;
  if (!ReadUInt8(&result_len)) {
    // OnFailure() already called.
    return false;
  }

  return ReadStringPiece(result, result_len);
}

bool QuicheDataReader::ReadStringPiece(absl::string_view* result, size_t size) {
  // Make sure that we have enough data to read.
  if (!CanRead(size)) {
    OnFailure();
    return false;
  }

  // Set result.
  *result = absl::string_view(data_ + pos_, size);

  // Iterate.
  pos_ += size;

  return true;
}

absl::string_view QuicheDataReader::ReadAtMost(size_t size) {
  size_t actual_size = std::min(size, BytesRemaining());
  absl::string_view result = absl::string_view(data_ + pos_, actual_size);
  AdvancePos(actual_size);
  return result;
}

bool QuicheDataReader::ReadTag(uint32_t* tag) {
  return ReadBytes(tag, sizeof(*tag));
}

bool QuicheDataReader::ReadDecimal64(size_t num_digits, uint64_t* result) {
  absl::string_view digits;
  if (!ReadStringPiece(&digits, num_digits)) {
    return false;
  }

  return absl::SimpleAtoi(digits, result);
}

QuicheVariableLengthIntegerLength QuicheDataReader::PeekVarInt62Length() {
  QUICHE_DCHECK_EQ(endianness(), NETWORK_BYTE_ORDER);
  const unsigned char* next =
      reinterpret_cast<const unsigned char*>(data() + pos());
  if (BytesRemaining() == 0) {
    return VARIABLE_LENGTH_INTEGER_LENGTH_0;
  }
  return static_cast<QuicheVariableLengthIntegerLength>(
      1 << ((*next & 0b11000000) >> 6));
}

// Read an RFC 9000 62-bit Variable Length Integer.
//
// Performance notes
//
// Measurements and experiments showed that unrolling the four cases
// like this and dereferencing next_ as we do (*(next_+n) --- and then
// doing a single pos_+=x at the end) gains about 10% over making a
// loop and dereferencing next_ such as *(next_++)
//
// Using a register for pos_ was not helpful.
//
// Branches are ordered to increase the likelihood of the first being
// taken.
//
// Low-level optimization is useful here because this function will be
// called frequently, leading to outsize benefits.
bool QuicheDataReader::ReadVarInt62(uint64_t* result) {
  QUICHE_DCHECK_EQ(endianness(), quiche::NETWORK_BYTE_ORDER);

  size_t remaining = BytesRemaining();
  const unsigned char* next =
      reinterpret_cast<const unsigned char*>(data() + pos());
  if (remaining != 0) {
    switch (*next & 0xc0) {
      case 0xc0:
        // Leading 0b11...... is 8 byte encoding
        if (remaining >= 8) {
          *result = (static_cast<uint64_t>((*(next)) & 0x3f) << 56) +
                    (static_cast<uint64_t>(*(next + 1)) << 48) +
                    (static_cast<uint64_t>(*(next + 2)) << 40) +
                    (static_cast<uint64_t>(*(next + 3)) << 32) +
                    (static_cast<uint64_t>(*(next + 4)) << 24) +
                    (static_cast<uint64_t>(*(next + 5)) << 16) +
                    (static_cast<uint64_t>(*(next + 6)) << 8) +
                    (static_cast<uint64_t>(*(next + 7)) << 0);
          AdvancePos(8);
          return true;
        }
        return false;

      case 0x80:
        // Leading 0b10...... is 4 byte encoding
        if (remaining >= 4) {
          *result = (((*(next)) & 0x3f) << 24) + (((*(next + 1)) << 16)) +
                    (((*(next + 2)) << 8)) + (((*(next + 3)) << 0));
          AdvancePos(4);
          return true;
        }
        return false;

      case 0x40:
        // Leading 0b01...... is 2 byte encoding
        if (remaining >= 2) {
          *result = (((*(next)) & 0x3f) << 8) + (*(next + 1));
          AdvancePos(2);
          return true;
        }
        return false;

      case 0x00:
        // Leading 0b00...... is 1 byte encoding
        *result = (*next) & 0x3f;
        AdvancePos(1);
        return true;
    }
  }
  return false;
}

bool QuicheDataReader::ReadStringPieceVarInt62(absl::string_view* result) {
  uint64_t result_length;
  if (!ReadVarInt62(&result_length)) {
    return false;
  }
  return ReadStringPiece(result, result_length);
}

bool QuicheDataReader::ReadStringVarInt62(std::string& result) {
  absl::string_view result_view;
  bool success = ReadStringPieceVarInt62(&result_view);
  result = std::string(result_view);
  return success;
}

absl::string_view QuicheDataReader::ReadRemainingPayload() {
  absl::string_view payload = PeekRemainingPayload();
  pos_ = len_;
  return payload;
}

absl::string_view QuicheDataReader::PeekRemainingPayload() const {
  return absl::string_view(data_ + pos_, len_ - pos_);
}

absl::string_view QuicheDataReader::FullPayload() const {
  return absl::string_view(data_, len_);
}

absl::string_view QuicheDataReader::PreviouslyReadPayload() const {
  return absl::string_view(data_, pos_);
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

bool QuicheDataReader::IsDoneReading() const { return len_ == pos_; }

size_t QuicheDataReader::BytesRemaining() const {
  if (pos_ > len_) {
    QUICHE_BUG(quiche_reader_pos_out_of_bound)
        << "QUIC reader pos out of bound: " << pos_ << ", len: " << len_;
    return 0;
  }
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
  return absl::StrCat(" { length: ", len_, ", position: ", pos_, " }");
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quiche
