// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_DATA_WRITER_H_
#define NET_QUIC_CORE_QUIC_DATA_WRITER_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "base/macros.h"
#include "net/base/int128.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_endian.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

// This class provides facilities for packing QUIC data.
//
// The QuicDataWriter supports appending primitive values (int, string, etc)
// to a frame instance.  The internal memory buffer is exposed as the "data"
// of the QuicDataWriter.
class QUIC_EXPORT_PRIVATE QuicDataWriter {
 public:
  // Creates a QuicDataWriter where |buffer| is not owned.
  QuicDataWriter(size_t size,
                 char* buffer,
                 Endianness endianness);

  ~QuicDataWriter();

  // Returns the size of the QuicDataWriter's data.
  size_t length() const { return length_; }

  // Retrieves the buffer from the QuicDataWriter without changing ownership.
  char* data();

  // Methods for adding to the payload.  These values are appended to the end
  // of the QuicDataWriter payload.

  // Writes 8/16/32/64-bit unsigned integers.
  bool WriteUInt8(uint8_t value);
  bool WriteUInt16(uint16_t value);
  bool WriteUInt32(uint32_t value);
  bool WriteUInt64(uint64_t value);

  // Writes |value| to the position |offset| from the start of the data.
  // |offset| must be less than the current length of the writer.
  bool WriteUInt8AtOffset(uint8_t value, size_t offset);

  // Writes least significant |num_bytes| of a 64-bit unsigned integer in the
  // correct byte order.
  bool WriteBytesToUInt64(size_t num_bytes, uint64_t value);

  // Write unsigned floating point corresponding to the value. Large values are
  // clamped to the maximum representable (kUFloat16MaxValue). Values that can
  // not be represented directly are rounded down.
  bool WriteUFloat16(uint64_t value);
  bool WriteStringPiece16(QuicStringPiece val);
  bool WriteBytes(const void* data, size_t data_len);
  bool WriteRepeatedByte(uint8_t byte, size_t count);
  // Fills the remaining buffer with null characters.
  void WritePadding();
  // Write padding of |count| bytes.
  bool WritePaddingBytes(size_t count);

  // Write connection ID as a 64-bit unsigned integer to the payload.
  // TODO(fayang): Remove this method and use WriteUInt64() once deprecating
  // quic_restart_flag_quic_rw_cid_in_big_endian and QuicDataWriter has a mode
  // indicating writing in little/big endian.
  bool WriteConnectionId(uint64_t connection_id);

  // Write tag as a 32-bit unsigned integer to the payload. As tags are already
  // converted to big endian (e.g., CHLO is 'C','H','L','O') in memory by TAG or
  // MakeQuicTag and tags are written in byte order, so tags on the wire are
  // in big endian.
  bool WriteTag(uint32_t tag);

  size_t capacity() const { return capacity_; }

 private:
  // Returns the location that the data should be written at, or nullptr if
  // there is not enough room. Call EndWrite with the returned offset and the
  // given length to pad out for the next write.
  char* BeginWrite(size_t length);

  char* buffer_;
  size_t capacity_;  // Allocation size of payload (or -1 if buffer is const).
  size_t length_;    // Current length of the buffer.

  // The endianness to write integers and floating numbers.
  Endianness endianness_;

  DISALLOW_COPY_AND_ASSIGN(QuicDataWriter);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_DATA_WRITER_H_
