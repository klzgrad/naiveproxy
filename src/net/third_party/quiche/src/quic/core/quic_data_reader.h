// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_DATA_READER_H_
#define QUICHE_QUIC_CORE_QUIC_DATA_READER_H_

#include <cstddef>
#include <cstdint>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_endian.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"

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
class QUIC_EXPORT_PRIVATE QuicDataReader {
 public:
  // Constructs a reader using NETWORK_BYTE_ORDER endianness.
  // Caller must provide an underlying buffer to work on.
  explicit QuicDataReader(QuicStringPiece data);
  // Constructs a reader using NETWORK_BYTE_ORDER endianness.
  // Caller must provide an underlying buffer to work on.
  QuicDataReader(const char* data, const size_t len);
  // Constructs a reader using the specified endianness.
  // Caller must provide an underlying buffer to work on.
  QuicDataReader(const char* data, const size_t len, Endianness endianness);
  QuicDataReader(const QuicDataReader&) = delete;
  QuicDataReader& operator=(const QuicDataReader&) = delete;

  // Empty destructor.
  ~QuicDataReader() {}

  // Reads an 8/16/32/64-bit unsigned integer into the given output
  // parameter. Forwards the internal iterator on success. Returns true on
  // success, false otherwise.
  bool ReadUInt8(uint8_t* result);
  bool ReadUInt16(uint16_t* result);
  bool ReadUInt32(uint32_t* result);
  bool ReadUInt64(uint64_t* result);

  // Set |result| to 0, then read |num_bytes| bytes in the correct byte order
  // into least significant bytes of |result|.
  bool ReadBytesToUInt64(size_t num_bytes, uint64_t* result);

  // Reads a 16-bit unsigned float into the given output parameter.
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadUFloat16(uint64_t* result);

  // Reads a string prefixed with 16-bit length into the given output parameter.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadStringPiece16(QuicStringPiece* result);

  // Reads a given number of bytes into the given buffer. The buffer
  // must be of adequate size.
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadStringPiece(QuicStringPiece* result, size_t len);

  // Reads connection ID into the given output parameter.
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadConnectionId(QuicConnectionId* connection_id, uint8_t length);

  // Reads 8-bit connection ID length followed by connection ID of that length.
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadLengthPrefixedConnectionId(QuicConnectionId* connection_id);

  // Reads tag represented as 32-bit unsigned integer into given output
  // parameter. Tags are in big endian on the wire (e.g., CHLO is
  // 'C','H','L','O') and are read in byte order, so tags in memory are in big
  // endian.
  bool ReadTag(uint32_t* tag);

  // Returns the remaining payload as a QuicStringPiece.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // Forwards the internal iterator.
  QuicStringPiece ReadRemainingPayload();

  // Returns the remaining payload as a QuicStringPiece.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // DOES NOT forward the internal iterator.
  QuicStringPiece PeekRemainingPayload() const;

  // Returns the entire payload as a QuicStringPiece.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // DOES NOT forward the internal iterator.
  QuicStringPiece FullPayload() const;

  // Reads a given number of bytes into the given buffer. The buffer
  // must be of adequate size.
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadBytes(void* result, size_t size);

  // Skips over |size| bytes from the buffer and forwards the internal iterator.
  // Returns true if there are at least |size| bytes remaining to read, false
  // otherwise.
  bool Seek(size_t size);

  // Returns true if the entirety of the underlying buffer has been read via
  // Read*() calls.
  bool IsDoneReading() const;

  // Returns the length in bytes of a variable length integer based on the next
  // two bits available. Returns 1, 2, 4, or 8 on success, and 0 on failure.
  QuicVariableLengthIntegerLength PeekVarInt62Length();

  // Returns the number of bytes remaining to be read.
  size_t BytesRemaining() const;

  // Truncates the reader down by reducing its internal length.
  // If called immediately after calling this, BytesRemaining will
  // return |truncation_length|. If truncation_length is less than the
  // current value of BytesRemaining, this does nothing and returns false.
  bool TruncateRemaining(size_t truncation_length);

  // Returns the next byte that to be read. Must not be called when there are no
  // bytes to be read.
  //
  // DOES NOT forward the internal iterator.
  uint8_t PeekByte() const;

  void set_endianness(Endianness endianness) { endianness_ = endianness; }

  // Read an IETF-encoded Variable Length Integer and place the result
  // in |*result|.
  // Returns true if it works, false if not. The only error is that
  // there is not enough in the buffer to read the number.
  // If there is an error, |*result| is not altered.
  // Numbers are encoded per the rules in draft-ietf-quic-transport-10.txt
  // and that the integers in the range 0 ... (2^62)-1.
  bool ReadVarInt62(uint64_t* result);

  // Convenience method that reads a uint32_t.
  // Attempts to read a varint into a uint32_t. using ReadVarInt62 and
  // returns false if there is a read error or if the value is
  // greater than (2^32)-1.
  bool ReadVarIntU32(uint32_t* result);

  std::string DebugString() const;

 private:
  // Returns true if the underlying buffer has enough room to read the given
  // amount of bytes.
  bool CanRead(size_t bytes) const;

  // To be called when a read fails for any reason.
  void OnFailure();

  // TODO(fkastenholz, b/73004262) change buffer_, et al, to be uint8_t, not
  // char. The data buffer that we're reading from.
  const char* data_;

  // The length of the data buffer that we're reading from.
  size_t len_;

  // The location of the next read from our data buffer.
  size_t pos_;

  // The endianness to read integers and floating numbers.
  Endianness endianness_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_DATA_READER_H_
