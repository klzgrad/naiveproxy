// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_data_reader.h"

#include "net/base/int128.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_utils.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"

namespace net {

#define ENDPOINT \
  (perspective_ == Perspective::IS_SERVER ? "Server: " : "Client: ")

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

bool QuicDataReader::ReadConnectionId(uint64_t* connection_id) {
  if (!ReadBytes(connection_id, sizeof(*connection_id))) {
    return false;
  }
  *connection_id = QuicEndian::NetToHost64(*connection_id);

  return true;
}

bool QuicDataReader::ReadTag(uint32_t* tag) {
  return ReadBytes(tag, sizeof(*tag));
}

QuicStringPiece QuicDataReader::ReadRemainingPayload() {
  QuicStringPiece payload = PeekRemainingPayload();
  pos_ = len_;
  return payload;
}

QuicStringPiece QuicDataReader::PeekRemainingPayload() {
  return QuicStringPiece(data_ + pos_, len_ - pos_);
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

bool QuicDataReader::IsDoneReading() const {
  return len_ == pos_;
}

size_t QuicDataReader::BytesRemaining() const {
  return len_ - pos_;
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

}  // namespace net
