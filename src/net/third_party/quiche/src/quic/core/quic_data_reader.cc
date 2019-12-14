// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"

namespace quic {

QuicDataReader::QuicDataReader(QuicStringPiece data)
    : QuicDataReader(data.data(), data.length(), NETWORK_BYTE_ORDER) {}

QuicDataReader::QuicDataReader(const char* data, const size_t len)
    : QuicDataReader(data, len, NETWORK_BYTE_ORDER) {}

QuicDataReader::QuicDataReader(const char* data,
                               const size_t len,
                               Endianness endianness)
    : data_(data), len_(len), pos_(0), endianness_(endianness) {}

bool QuicDataReader::ReadUInt8(uint8_t* result) {
  return ReadBytes(result, sizeof(*result));
}

bool QuicDataReader::ReadUInt16(uint16_t* result) {
  if (!ReadBytes(result, sizeof(*result))) {
    return false;
  }
  if (endianness_ == NETWORK_BYTE_ORDER) {
    *result = QuicEndian::NetToHost16(*result);
  }
  return true;
}

bool QuicDataReader::ReadUInt32(uint32_t* result) {
  if (!ReadBytes(result, sizeof(*result))) {
    return false;
  }
  if (endianness_ == NETWORK_BYTE_ORDER) {
    *result = QuicEndian::NetToHost32(*result);
  }
  return true;
}

bool QuicDataReader::ReadUInt64(uint64_t* result) {
  if (!ReadBytes(result, sizeof(*result))) {
    return false;
  }
  if (endianness_ == NETWORK_BYTE_ORDER) {
    *result = QuicEndian::NetToHost64(*result);
  }
  return true;
}

bool QuicDataReader::ReadBytesToUInt64(size_t num_bytes, uint64_t* result) {
  *result = 0u;
  if (num_bytes > sizeof(*result)) {
    return false;
  }
  if (endianness_ == HOST_BYTE_ORDER) {
    return ReadBytes(result, num_bytes);
  }

  if (!ReadBytes(reinterpret_cast<char*>(result) + sizeof(*result) - num_bytes,
                 num_bytes)) {
    return false;
  }
  *result = QuicEndian::NetToHost64(*result);
  return true;
}

bool QuicDataReader::ReadUFloat16(uint64_t* result) {
  uint16_t value;
  if (!ReadUInt16(&value)) {
    return false;
  }

  *result = value;
  if (*result < (1 << kUFloat16MantissaEffectiveBits)) {
    // Fast path: either the value is denormalized (no hidden bit), or
    // normalized (hidden bit set, exponent offset by one) with exponent zero.
    // Zero exponent offset by one sets the bit exactly where the hidden bit is.
    // So in both cases the value encodes itself.
    return true;
  }

  uint16_t exponent =
      value >> kUFloat16MantissaBits;  // No sign extend on uint!
  // After the fast pass, the exponent is at least one (offset by one).
  // Un-offset the exponent.
  --exponent;
  DCHECK_GE(exponent, 1);
  DCHECK_LE(exponent, kUFloat16MaxExponent);
  // Here we need to clear the exponent and set the hidden bit. We have already
  // decremented the exponent, so when we subtract it, it leaves behind the
  // hidden bit.
  *result -= exponent << kUFloat16MantissaBits;
  *result <<= exponent;
  DCHECK_GE(*result,
            static_cast<uint64_t>(1 << kUFloat16MantissaEffectiveBits));
  DCHECK_LE(*result, kUFloat16MaxValue);
  return true;
}

bool QuicDataReader::ReadStringPiece16(QuicStringPiece* result) {
  // Read resultant length.
  uint16_t result_len;
  if (!ReadUInt16(&result_len)) {
    // OnFailure() already called.
    return false;
  }

  return ReadStringPiece(result, result_len);
}

bool QuicDataReader::ReadStringPiece(QuicStringPiece* result, size_t size) {
  // Make sure that we have enough data to read.
  if (!CanRead(size)) {
    OnFailure();
    return false;
  }

  // Set result.
  *result = QuicStringPiece(data_ + pos_, size);

  // Iterate.
  pos_ += size;

  return true;
}

bool QuicDataReader::ReadConnectionId(QuicConnectionId* connection_id,
                                      uint8_t length) {
  if (length > kQuicMaxConnectionIdAllVersionsLength) {
    QUIC_BUG << "Attempted to read connection ID with length too high "
             << static_cast<int>(length);
    return false;
  }
  if (length == 0) {
    connection_id->set_length(0);
    return true;
  }

  if (BytesRemaining() < length) {
    return false;
  }

  connection_id->set_length(length);
  const bool ok =
      ReadBytes(connection_id->mutable_data(), connection_id->length());
  DCHECK(ok);
  return ok;
}

bool QuicDataReader::ReadLengthPrefixedConnectionId(
    QuicConnectionId* connection_id) {
  uint8_t connection_id_length;
  if (!ReadUInt8(&connection_id_length)) {
    return false;
  }
  if (connection_id_length > kQuicMaxConnectionIdAllVersionsLength) {
    return false;
  }
  return ReadConnectionId(connection_id, connection_id_length);
}

bool QuicDataReader::ReadTag(uint32_t* tag) {
  return ReadBytes(tag, sizeof(*tag));
}

QuicStringPiece QuicDataReader::ReadRemainingPayload() {
  QuicStringPiece payload = PeekRemainingPayload();
  pos_ = len_;
  return payload;
}

QuicStringPiece QuicDataReader::PeekRemainingPayload() const {
  return QuicStringPiece(data_ + pos_, len_ - pos_);
}

QuicStringPiece QuicDataReader::FullPayload() const {
  return QuicStringPiece(data_, len_);
}

bool QuicDataReader::ReadBytes(void* result, size_t size) {
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

bool QuicDataReader::Seek(size_t size) {
  if (!CanRead(size)) {
    OnFailure();
    return false;
  }
  pos_ += size;
  return true;
}

bool QuicDataReader::IsDoneReading() const {
  return len_ == pos_;
}

QuicVariableLengthIntegerLength QuicDataReader::PeekVarInt62Length() {
  DCHECK_EQ(endianness_, NETWORK_BYTE_ORDER);
  const unsigned char* next =
      reinterpret_cast<const unsigned char*>(data_ + pos_);
  if (BytesRemaining() == 0) {
    return VARIABLE_LENGTH_INTEGER_LENGTH_0;
  }
  return static_cast<QuicVariableLengthIntegerLength>(
      1 << ((*next & 0b11000000) >> 6));
}

size_t QuicDataReader::BytesRemaining() const {
  return len_ - pos_;
}

bool QuicDataReader::TruncateRemaining(size_t truncation_length) {
  if (truncation_length > BytesRemaining()) {
    return false;
  }
  len_ = pos_ + truncation_length;
  return true;
}

bool QuicDataReader::CanRead(size_t bytes) const {
  return bytes <= (len_ - pos_);
}

void QuicDataReader::OnFailure() {
  // Set our iterator to the end of the buffer so that further reads fail
  // immediately.
  pos_ = len_;
}

uint8_t QuicDataReader::PeekByte() const {
  if (pos_ >= len_) {
    QUIC_BUG << "Reading is done, cannot peek next byte. Tried to read pos = "
             << pos_ << " buffer length = " << len_;
    return 0;
  }
  return data_[pos_];
}

// Read an IETF/QUIC formatted 62-bit Variable Length Integer.
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
bool QuicDataReader::ReadVarInt62(uint64_t* result) {
  DCHECK_EQ(endianness_, NETWORK_BYTE_ORDER);

  size_t remaining = BytesRemaining();
  const unsigned char* next =
      reinterpret_cast<const unsigned char*>(data_ + pos_);
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
          pos_ += 8;
          return true;
        }
        return false;

      case 0x80:
        // Leading 0b10...... is 4 byte encoding
        if (remaining >= 4) {
          *result = (((*(next)) & 0x3f) << 24) + (((*(next + 1)) << 16)) +
                    (((*(next + 2)) << 8)) + (((*(next + 3)) << 0));
          pos_ += 4;
          return true;
        }
        return false;

      case 0x40:
        // Leading 0b01...... is 2 byte encoding
        if (remaining >= 2) {
          *result = (((*(next)) & 0x3f) << 8) + (*(next + 1));
          pos_ += 2;
          return true;
        }
        return false;

      case 0x00:
        // Leading 0b00...... is 1 byte encoding
        *result = (*next) & 0x3f;
        pos_++;
        return true;
    }
  }
  return false;
}

bool QuicDataReader::ReadVarIntU32(uint32_t* result) {
  uint64_t temp_uint64;
  // TODO(fkastenholz): We should disambiguate read-errors from
  // value errors.
  if (!this->ReadVarInt62(&temp_uint64)) {
    return false;
  }
  if (temp_uint64 > kMaxQuicStreamId) {
    return false;
  }
  *result = static_cast<uint32_t>(temp_uint64);
  return true;
}

std::string QuicDataReader::DebugString() const {
  return QuicStrCat(" { length: ", len_, ", position: ", pos_, " }");
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
