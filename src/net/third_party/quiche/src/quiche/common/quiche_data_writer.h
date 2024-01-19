// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_DATA_WRITER_H_
#define QUICHE_COMMON_QUICHE_DATA_WRITER_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"

namespace quiche {

// Maximum value that can be properly encoded using RFC 9000 62-bit Variable
// Length Integer encoding.
enum : uint64_t {
  kVarInt62MaxValue = UINT64_C(0x3fffffffffffffff),
};

// RFC 9000 62-bit Variable Length Integer encoding masks
// If a uint64_t anded with a mask is not 0 then the value is encoded
// using that length (or is too big, in the case of kVarInt62ErrorMask).
// Values must be checked in order (error, 8-, 4-, and then 2- bytes)
// and if none are non-0, the value is encoded in 1 byte.
enum : uint64_t {
  kVarInt62ErrorMask = UINT64_C(0xc000000000000000),
  kVarInt62Mask8Bytes = UINT64_C(0x3fffffffc0000000),
  kVarInt62Mask4Bytes = UINT64_C(0x000000003fffc000),
  kVarInt62Mask2Bytes = UINT64_C(0x0000000000003fc0),
};

// This class provides facilities for packing binary data.
//
// The QuicheDataWriter supports appending primitive values (int, string, etc)
// to a frame instance.  The internal memory buffer is exposed as the "data"
// of the QuicheDataWriter.
class QUICHE_EXPORT QuicheDataWriter {
 public:
  // Creates a QuicheDataWriter where |buffer| is not owned
  // using NETWORK_BYTE_ORDER endianness.
  QuicheDataWriter(size_t size, char* buffer);
  // Creates a QuicheDataWriter where |buffer| is not owned
  // using the specified endianness.
  QuicheDataWriter(size_t size, char* buffer, quiche::Endianness endianness);
  QuicheDataWriter(const QuicheDataWriter&) = delete;
  QuicheDataWriter& operator=(const QuicheDataWriter&) = delete;

  ~QuicheDataWriter();

  // Returns the size of the QuicheDataWriter's data.
  size_t length() const { return length_; }

  // Retrieves the buffer from the QuicheDataWriter without changing ownership.
  char* data();

  // Methods for adding to the payload.  These values are appended to the end
  // of the QuicheDataWriter payload.

  // Writes 8/16/32/64-bit unsigned integers.
  bool WriteUInt8(uint8_t value);
  bool WriteUInt16(uint16_t value);
  bool WriteUInt32(uint32_t value);
  bool WriteUInt64(uint64_t value);

  // Writes least significant |num_bytes| of a 64-bit unsigned integer in the
  // correct byte order.
  bool WriteBytesToUInt64(size_t num_bytes, uint64_t value);

  bool WriteStringPiece(absl::string_view val);
  bool WriteStringPiece16(absl::string_view val);
  bool WriteBytes(const void* data, size_t data_len);
  bool WriteRepeatedByte(uint8_t byte, size_t count);
  // Fills the remaining buffer with null characters.
  void WritePadding();
  // Write padding of |count| bytes.
  bool WritePaddingBytes(size_t count);

  // Write tag as a 32-bit unsigned integer to the payload. As tags are already
  // converted to big endian (e.g., CHLO is 'C','H','L','O') in memory by TAG or
  // MakeQuicTag and tags are written in byte order, so tags on the wire are
  // in big endian.
  bool WriteTag(uint32_t tag);

  // Write a 62-bit unsigned integer using RFC 9000 Variable Length Integer
  // encoding. Returns false if the value is out of range or if there is no room
  // in the buffer.
  bool WriteVarInt62(uint64_t value);

  // Same as WriteVarInt62(uint64_t), but forces an encoding size to write to.
  // This is not as optimized as WriteVarInt62(uint64_t). Returns false if the
  // value does not fit in the specified write_length or if there is no room in
  // the buffer.
  bool WriteVarInt62WithForcedLength(
      uint64_t value, QuicheVariableLengthIntegerLength write_length);

  // Writes a string piece as a consecutive length/content pair. The
  // length uses RFC 9000 Variable Length Integer encoding.
  bool WriteStringPieceVarInt62(const absl::string_view& string_piece);

  // Utility function to return the number of bytes needed to encode
  // the given value using IETF VarInt62 encoding. Returns the number
  // of bytes required to encode the given integer or 0 if the value
  // is too large to encode.
  static QuicheVariableLengthIntegerLength GetVarInt62Len(uint64_t value);

  // Advance the writer's position for writing by |length| bytes without writing
  // anything. This method only makes sense to be used on a buffer that has
  // already been written to (and is having certain parts rewritten).
  bool Seek(size_t length);

  size_t capacity() const { return capacity_; }

  size_t remaining() const { return capacity_ - length_; }

  std::string DebugString() const;

 protected:
  // Returns the location that the data should be written at, or nullptr if
  // there is not enough room. Call EndWrite with the returned offset and the
  // given length to pad out for the next write.
  char* BeginWrite(size_t length);

  quiche::Endianness endianness() const { return endianness_; }

  char* buffer() const { return buffer_; }

  void IncreaseLength(size_t delta) {
    QUICHE_DCHECK_LE(length_, std::numeric_limits<size_t>::max() - delta);
    QUICHE_DCHECK_LE(length_, capacity_ - delta);
    length_ += delta;
  }

 private:
  // TODO(fkastenholz, b/73004262) change buffer_, et al, to be uint8_t, not
  // char.
  char* buffer_;
  size_t capacity_;  // Allocation size of payload (or -1 if buffer is const).
  size_t length_;    // Current length of the buffer.

  // The endianness to write integers and floating numbers.
  quiche::Endianness endianness_;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_DATA_WRITER_H_
