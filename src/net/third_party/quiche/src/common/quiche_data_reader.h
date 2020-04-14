// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_DATA_READER_H_
#define QUICHE_COMMON_QUICHE_DATA_READER_H_

#include <cstddef>
#include <cstdint>
#include <limits>

#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

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
class QUICHE_EXPORT_PRIVATE QuicheDataReader {
 public:
  // Constructs a reader using NETWORK_BYTE_ORDER endianness.
  // Caller must provide an underlying buffer to work on.
  explicit QuicheDataReader(quiche::QuicheStringPiece data);
  // Constructs a reader using NETWORK_BYTE_ORDER endianness.
  // Caller must provide an underlying buffer to work on.
  QuicheDataReader(const char* data, const size_t len);
  // Constructs a reader using the specified endianness.
  // Caller must provide an underlying buffer to work on.
  QuicheDataReader(const char* data,
                   const size_t len,
                   quiche::Endianness endianness);
  QuicheDataReader(const QuicheDataReader&) = delete;
  QuicheDataReader& operator=(const QuicheDataReader&) = delete;

  // Empty destructor.
  ~QuicheDataReader() {}

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

  // Reads a string prefixed with 16-bit length into the given output parameter.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadStringPiece16(quiche::QuicheStringPiece* result);

  // Reads a given number of bytes into the given buffer. The buffer
  // must be of adequate size.
  // Forwards the internal iterator on success.
  // Returns true on success, false otherwise.
  bool ReadStringPiece(quiche::QuicheStringPiece* result, size_t size);

  // Reads tag represented as 32-bit unsigned integer into given output
  // parameter. Tags are in big endian on the wire (e.g., CHLO is
  // 'C','H','L','O') and are read in byte order, so tags in memory are in big
  // endian.
  bool ReadTag(uint32_t* tag);

  // Returns the remaining payload as a quiche::QuicheStringPiece.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // Forwards the internal iterator.
  quiche::QuicheStringPiece ReadRemainingPayload();

  // Returns the remaining payload as a quiche::QuicheStringPiece.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // DOES NOT forward the internal iterator.
  quiche::QuicheStringPiece PeekRemainingPayload() const;

  // Returns the entire payload as a quiche::QuicheStringPiece.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // DOES NOT forward the internal iterator.
  quiche::QuicheStringPiece FullPayload() const;

  // Returns the part of the payload that has been already read as a
  // quiche::QuicheStringPiece.
  //
  // NOTE: Does not copy but rather references strings in the underlying buffer.
  // This should be kept in mind when handling memory management!
  //
  // DOES NOT forward the internal iterator.
  quiche::QuicheStringPiece PreviouslyReadPayload() const;

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
  // return |truncation_length|. If truncation_length is less than the
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
    DCHECK_LE(pos_, std::numeric_limits<size_t>::max() - amount);
    DCHECK_LE(pos_, len_ - amount);
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
