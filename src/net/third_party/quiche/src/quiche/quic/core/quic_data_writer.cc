// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_data_writer.h"

#include <algorithm>
#include <limits>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/quiche_endian.h"

namespace quic {

QuicDataWriter::QuicDataWriter(size_t size, char* buffer)
    : quiche::QuicheDataWriter(size, buffer) {}

QuicDataWriter::QuicDataWriter(size_t size, char* buffer,
                               quiche::Endianness endianness)
    : quiche::QuicheDataWriter(size, buffer, endianness) {}

QuicDataWriter::~QuicDataWriter() {}

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

    QUICHE_DCHECK_GE(exponent, 1);
    QUICHE_DCHECK_LE(exponent, kUFloat16MaxExponent);
    QUICHE_DCHECK_GE(value, UINT64_C(1) << kUFloat16MantissaBits);
    QUICHE_DCHECK_LT(value, UINT64_C(1) << kUFloat16MantissaEffectiveBits);

    // Hidden bit (position 11) is set. We should remove it and increment the
    // exponent. Equivalently, we just add it to the exponent.
    // This hides the bit.
    result = static_cast<uint16_t>(value + (exponent << kUFloat16MantissaBits));
  }

  if (endianness() == quiche::NETWORK_BYTE_ORDER) {
    result = quiche::QuicheEndian::HostToNet16(result);
  }
  return WriteBytes(&result, sizeof(result));
}

bool QuicDataWriter::WriteConnectionId(QuicConnectionId connection_id) {
  if (connection_id.IsEmpty()) {
    return true;
  }
  return WriteBytes(connection_id.data(), connection_id.length());
}

bool QuicDataWriter::WriteLengthPrefixedConnectionId(
    QuicConnectionId connection_id) {
  return WriteUInt8(connection_id.length()) && WriteConnectionId(connection_id);
}

bool QuicDataWriter::WriteRandomBytes(QuicRandom* random, size_t length) {
  char* dest = BeginWrite(length);
  if (!dest) {
    return false;
  }

  random->RandBytes(dest, length);
  IncreaseLength(length);
  return true;
}

bool QuicDataWriter::WriteInsecureRandomBytes(QuicRandom* random,
                                              size_t length) {
  char* dest = BeginWrite(length);
  if (!dest) {
    return false;
  }

  random->InsecureRandBytes(dest, length);
  IncreaseLength(length);
  return true;
}

}  // namespace quic
