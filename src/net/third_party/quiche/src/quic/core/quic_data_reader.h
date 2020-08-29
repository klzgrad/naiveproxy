// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_DATA_READER_H_
#define QUICHE_QUIC_CORE_QUIC_DATA_READER_H_

#include <cstddef>
#include <cstdint>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/quiche_data_reader.h"

namespace quic {

// Used for reading QUIC data. Though there isn't really anything terribly
// QUIC-specific here, it's a helper class that's useful when doing QUIC
// framing.
//
// To use, simply construct a QuicDataReader using the underlying buffer that
// you'd like to read fields from, then call one of the Read*() methods to
// actually do some reading.
//
// This class keeps an internal iterator to keep track of what's already been
// read and each successive Read*() call automatically increments said iterator
// on success. On failure, internal state of the QuicDataReader should not be
// trusted and it is up to the caller to throw away the failed instance and
// handle the error as appropriate. None of the Read*() methods should ever be
// called after failure, as they will also fail immediately.
class QUIC_EXPORT_PRIVATE QuicDataReader : public quiche::QuicheDataReader {
 public:
  // Constructs a reader using NETWORK_BYTE_ORDER endianness.
  // Caller must provide an underlying buffer to work on.
  explicit QuicDataReader(quiche::QuicheStringPiece data);
  // Constructs a reader using NETWORK_BYTE_ORDER endianness.
  // Caller must provide an underlying buffer to work on.
  QuicDataReader(const char* data, const size_t len);
  // Constructs a reader using the specified endianness.
  // Caller must provide an underlying buffer to work on.
  QuicDataReader(const char* data,
                 const size_t len,
                 quiche::Endianness endianness);
  QuicDataReader(const QuicDataReader&) = delete;
  QuicDataReader& operator=(const QuicDataReader&) = delete;

  // Empty destructor.
  ~QuicDataReader() {}

  // Reads a 16-bit unsigned float into the given output parameter.
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadUFloat16(uint64_t* result);

  // Reads connection ID into the given output parameter.
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadConnectionId(QuicConnectionId* connection_id, uint8_t length);

  // Reads 8-bit connection ID length followed by connection ID of that length.
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadLengthPrefixedConnectionId(QuicConnectionId* connection_id);

  // Returns the length in bytes of a variable length integer based on the next
  // two bits available. Returns 1, 2, 4, or 8 on success, and 0 on failure.
  QuicVariableLengthIntegerLength PeekVarInt62Length();

  // Read an IETF-encoded Variable Length Integer and place the result
  // in |*result|.
  // Returns true if it works, false if not. The only error is that
  // there is not enough in the buffer to read the number.
  // If there is an error, |*result| is not altered.
  // Numbers are encoded per the rules in draft-ietf-quic-transport-10.txt
  // and that the integers in the range 0 ... (2^62)-1.
  bool ReadVarInt62(uint64_t* result);

  // Reads a string prefixed with a Variable Length integer length into the
  // given output parameter.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadStringPieceVarInt62(quiche::QuicheStringPiece* result);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_DATA_READER_H_
