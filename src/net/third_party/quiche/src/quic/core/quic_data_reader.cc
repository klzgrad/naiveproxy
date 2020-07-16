// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QuicDataReader::QuicDataReader(quiche::QuicheStringPiece data)
    : quiche::QuicheDataReader(data) {}

QuicDataReader::QuicDataReader(const char* data, const size_t len)
    : QuicDataReader(data, len, quiche::NETWORK_BYTE_ORDER) {}

QuicDataReader::QuicDataReader(const char* data,
                               const size_t len,
                               quiche::Endianness endianness)
    : quiche::QuicheDataReader(data, len, endianness) {}

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

bool QuicDataReader::ReadConnectionId(QuicConnectionId* connection_id,
                                      uint8_t length) {
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
  return ReadConnectionId(connection_id, connection_id_length);
}

QuicVariableLengthIntegerLength QuicDataReader::PeekVarInt62Length() {
  DCHECK_EQ(endianness(), quiche::NETWORK_BYTE_ORDER);
  const unsigned char* next =
      reinterpret_cast<const unsigned char*>(data() + pos());
  if (BytesRemaining() == 0) {
    return VARIABLE_LENGTH_INTEGER_LENGTH_0;
  }
  return static_cast<QuicVariableLengthIntegerLength>(
      1 << ((*next & 0b11000000) >> 6));
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
  DCHECK_EQ(endianness(), quiche::NETWORK_BYTE_ORDER);

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

bool QuicDataReader::ReadStringPieceVarInt62(
    quiche::QuicheStringPiece* result) {
  uint64_t result_length;
  if (!ReadVarInt62(&result_length)) {
    return false;
  }
  return ReadStringPiece(result, result_length);
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
