// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_DATA_READER_H_
#define QUICHE_COMMON_QUICHE_DATA_READER_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"

namespace quiche {

// To use, simply construct a QuicheDataReader using the underlying buffer that
// you'd like to read fields from, then call one of the Read*() methods to
// actually do some reading.
//
// This class keeps an internal iterator to keep track of what's already been
// read and each successive Read*() call automatically increments said iterator
// on success. On failure, internal state of the QuicheDataReader should not be
// trusted and it is up to the caller to throw away the failed instance and
// handle the error as appropriate. None of the Read*() methods should ever be
// called after failure, as they will also fail immediately.
class QUICHE_EXPORT QuicheDataReader {
 public:
  // Constructs a reader using NETWORK_BYTE_ORDER endianness.
  // Caller must provide an underlying buffer to work on.
  explicit QuicheDataReader(absl::string_view data);
  // Constructs a reader using NETWORK_BYTE_ORDER endianness.
  // Caller must provide an underlying buffer to work on.
  QuicheDataReader(const char* data, const size_t len);
  // Constructs a reader using the specified endianness.
  // Caller must provide an underlying buffer to work on.
  QuicheDataReader(const char* data, const size_t len,
                   quiche::Endianness endianness);
  QuicheDataReader(const QuicheDataReader&) = delete;
  QuicheDataReader& operator=(const QuicheDataReader&) = delete;

  // Empty destructor.
  ~QuicheDataReader() {}

  // Reads an 8/16/24/32/64-bit unsigned integer into the given output
  // parameter. Forwards the internal iterator on success. Returns true on
  // success, false otherwise.
  bool ReadUInt8(uint8_t* result);
  bool ReadUInt16(uint16_t* result);
  bool ReadUInt24(uint32_t* result);
  bool ReadUInt32(uint32_t* result);
  bool ReadUInt64(uint64_t* result);

  // Set |result| to 0, then read |num_bytes| bytes in the correct byte order
  // into least significant bytes of |result|.
  bool ReadBytesToUInt64(size_t num_bytes, uint64_t* result);

  // Reads a string prefixed with 16-bit length into the given output parameter.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadStringPiece16(absl::string_view* result);

  // Reads a string prefixed with 8-bit length into the given output parameter.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadStringPiece8(absl::string_view* result);

  // Reads a given number of bytes into the given buffer. The buffer
  // must be of adequate size.
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadStringPiece(absl::string_view* result, size_t size);

  // Reads tag represented as 32-bit unsigned integer into given output
  // parameter. Tags are in big endian on the wire (e.g., CHLO is
  // 'C','H','L','O') and are read in byte order, so tags in memory are in big
  // endian.
  bool ReadTag(uint32_t* tag);

  // Reads a sequence of a fixed number of decimal digits, parses them as an
  // unsigned integer and returns them as a uint64_t.  Forwards internal
  // iterator on success, may forward it even in case of failure.
  bool ReadDecimal64(size_t num_digits, uint64_t* result);

  // Returns the length in bytes of a variable length integer based on the next
  // two bits available. Returns 1, 2, 4, or 8 on success, and 0 on failure.
  QuicheVariableLengthIntegerLength PeekVarInt62Length();

  // Read an RFC 9000 62-bit Variable Length Integer and place the result in
  // |*result|. Returns false if there is not enough space in the buffer to read
  // the number, true otherwise. If false is returned, |*result| is not altered.
  bool ReadVarInt62(uint64_t* result);

  // Reads a string prefixed with a RFC 9000 62-bit variable Length integer
  // length into the given output parameter.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // Returns false if there is not enough space in the buffer to read
  // the number and subsequent string, true otherwise.
  bool ReadStringPieceVarInt62(absl::string_view* result);

  // Reads a string prefixed with a RFC 9000 varint length prefix, and copies it
  // into the provided string.
  //
  // Returns false if there is not enough space in the buffer to read
  // the number and subsequent string, true otherwise.
  bool ReadStringVarInt62(std::string& result);

  // Returns the remaining payload as a absl::string_view.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // Forwards the internal iterator.
  absl::string_view ReadRemainingPayload();

  // Returns the remaining payload as a absl::string_view.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // DOES NOT forward the internal iterator.
  absl::string_view PeekRemainingPayload() const;

  // Returns the entire payload as a absl::string_view.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // DOES NOT forward the internal iterator.
  absl::string_view FullPayload() const;

  // Returns the part of the payload that has been already read as a
  // absl::string_view.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // DOES NOT forward the internal iterator.
  absl::string_view PreviouslyReadPayload() const;

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

  // Returns the number of bytes remaining to be read.
  size_t BytesRemaining() const;

  // Truncates the reader down by reducing its internal length.
  // If called immediately after calling this, BytesRemaining will
  // return |truncation_length|. If truncation_length is greater than the
  // current value of BytesRemaining, this does nothing and returns false.
  bool TruncateRemaining(size_t truncation_length);

  // Returns the next byte that to be read. Must not be called when there are no
  // bytes to be read.
  //
  // DOES NOT forward the internal iterator.
  uint8_t PeekByte() const;

  std::string DebugString() const;

 protected:
  // Returns true if the underlying buffer has enough room to read the given
  // amount of bytes.
  bool CanRead(size_t bytes) const;

  // To be called when a read fails for any reason.
  void OnFailure();

  const char* data() const { return data_; }

  size_t pos() const { return pos_; }

  void AdvancePos(size_t amount) {
    QUICHE_DCHECK_LE(pos_, std::numeric_limits<size_t>::max() - amount);
    QUICHE_DCHECK_LE(pos_, len_ - amount);
    pos_ += amount;
  }

  quiche::Endianness endianness() const { return endianness_; }

 private:
  // TODO(fkastenholz, b/73004262) change buffer_, et al, to be uint8_t, not
  // char. The data buffer that we're reading from.
  const char* data_;

  // The length of the data buffer that we're reading from.
  size_t len_;

  // The location of the next read from our data buffer.
  size_t pos_;

  // The endianness to read integers and floating numbers.
  quiche::Endianness endianness_;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_DATA_READER_H_
