// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_UTILS_H_
#define NET_QUIC_CORE_QUIC_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "base/macros.h"
#include "net/base/int128.h"
#include "net/quic/core/quic_error_codes.h"
#include "net/quic/core/quic_iovector.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_socket_address.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

class QUIC_EXPORT_PRIVATE QuicUtils {
 public:
  // Returns the 64 bit FNV1a hash of the data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static uint64_t FNV1a_64_Hash(QuicStringPiece data);

  // Returns the 128 bit FNV1a hash of the data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static uint128 FNV1a_128_Hash(QuicStringPiece data);

  // Returns the 128 bit FNV1a hash of the two sequences of data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static uint128 FNV1a_128_Hash_Two(QuicStringPiece data1,
                                    QuicStringPiece data2);

  // Returns the 128 bit FNV1a hash of the three sequences of data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static uint128 FNV1a_128_Hash_Three(QuicStringPiece data1,
                                      QuicStringPiece data2,
                                      QuicStringPiece data3);

  // SerializeUint128 writes the first 96 bits of |v| in little-endian form
  // to |out|.
  static void SerializeUint128Short(uint128 v, uint8_t* out);

  // Returns the level of encryption as a char*
  static const char* EncryptionLevelToString(EncryptionLevel level);

  // Returns TransmissionType as a char*
  static const char* TransmissionTypeToString(TransmissionType type);

  // Returns PeerAddressChangeType as a std::string.
  static std::string PeerAddressChangeTypeToString(PeerAddressChangeType type);

  // Determines and returns change type of address change from |old_address| to
  // |new_address|.
  static PeerAddressChangeType DetermineAddressChangeType(
      const QuicSocketAddress& old_address,
      const QuicSocketAddress& new_address);

  // Copies |length| bytes from iov starting at offset |iov_offset| into buffer.
  // |iov| must be at least iov_offset+length total length and buffer must be
  // at least |length| long.
  static void CopyToBuffer(QuicIOVector iov,
                           size_t iov_offset,
                           size_t length,
                           char* buffer);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicUtils);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_UTILS_H_
