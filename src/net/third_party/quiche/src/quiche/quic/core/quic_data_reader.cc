// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_data_reader.h"

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/quiche_endian.h"

namespace quic {

QuicDataReader::QuicDataReader(absl::string_view data)
    : quiche::QuicheDataReader(data) {}

QuicDataReader::QuicDataReader(const char* data, const size_t len)
    : QuicDataReader(data, len, quiche::NETWORK_BYTE_ORDER) {}

QuicDataReader::QuicDataReader(const char* data, const size_t len,
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
  QUICHE_DCHECK_GE(exponent, 1);
  QUICHE_DCHECK_LE(exponent, kUFloat16MaxExponent);
  // Here we need to clear the exponent and set the hidden bit. We have already
  // decremented the exponent, so when we subtract it, it leaves behind the
  // hidden bit.
  *result -= exponent << kUFloat16MantissaBits;
  *result <<= exponent;
  QUICHE_DCHECK_GE(*result,
                   static_cast<uint64_t>(1 << kUFloat16MantissaEffectiveBits));
  QUICHE_DCHECK_LE(*result, kUFloat16MaxValue);
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
  QUICHE_DCHECK(ok);
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

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
