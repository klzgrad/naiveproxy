// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_QUIC_HTTP_DECODE_BUFFER_H_
#define NET_QUIC_HTTP_DECODER_QUIC_HTTP_DECODE_BUFFER_H_

// QuicHttpDecodeBuffer provides primitives for decoding various integer types
// found in HTTP/2 frames. It wraps a byte array from which we can read and
// decode serialized HTTP/2 frames, or parts thereof. QuicHttpDecodeBuffer is
// intended only for stack allocation, where the caller is typically going to
// use the QuicHttpDecodeBuffer instance as part of decoding the entire buffer
// before returning to its own caller.

#include <stddef.h>
#include <stdint.h>

#include <algorithm>

#include "base/logging.h"
#include "base/macros.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {
class QuicHttpDecodeBufferSubset;

class QUIC_EXPORT_PRIVATE QuicHttpDecodeBuffer {
 public:
  QuicHttpDecodeBuffer(const char* buffer, size_t len)
      : buffer_(buffer), cursor_(buffer), beyond_(buffer + len) {
    DCHECK(buffer != nullptr);
    DCHECK_LE(len, MaxQuicHttpDecodeBufferLength());
  }
  explicit QuicHttpDecodeBuffer(QuicStringPiece s)
      : QuicHttpDecodeBuffer(s.data(), s.size()) {}
  // Constructor for character arrays, typically in tests. For example:
  //    const char input[] = { 0x11 };
  //    QuicHttpDecodeBuffer b(input);
  template <size_t N>
  explicit QuicHttpDecodeBuffer(const char (&buf)[N])
      : QuicHttpDecodeBuffer(buf, N) {}

  bool Empty() const { return cursor_ >= beyond_; }
  bool HasData() const { return cursor_ < beyond_; }
  size_t Remaining() const {
    DCHECK_LE(cursor_, beyond_);
    return beyond_ - cursor_;
  }
  size_t Offset() const { return cursor_ - buffer_; }
  size_t FullSize() const { return beyond_ - buffer_; }

  // Returns the minimum of the number of bytes remaining in this
  // QuicHttpDecodeBuffer and |length|, in support of determining how much of
  // some structure/quic_http_payload is in this QuicHttpDecodeBuffer.
  size_t MinLengthRemaining(size_t length) const {
    return std::min(length, Remaining());
  }

  // For std::string decoding, returns a pointer to the next byte/char to be
  // decoded.
  const char* cursor() const { return cursor_; }
  // Advances the cursor (pointer to the next byte/char to be decoded).
  void AdvanceCursor(size_t amount) {
    DCHECK_LE(amount, Remaining());  // Need at least that much remaining.
    DCHECK_EQ(subset_, nullptr) << "Access via subset only when present.";
    cursor_ += amount;
  }

  // Only call methods starting "Decode" when there is enough input remaining.
  char DecodeChar() {
    DCHECK_LE(1u, Remaining());  // Need at least one byte remaining.
    DCHECK_EQ(subset_, nullptr) << "Access via subset only when present.";
    return *cursor_++;
  }

  uint8_t DecodeUInt8() { return static_cast<uint8_t>(DecodeChar()); }

  uint16_t DecodeUInt16() {
    DCHECK_LE(2u, Remaining());
    const uint8_t b1 = DecodeUInt8();
    const uint8_t b2 = DecodeUInt8();
    // Note that chars are automatically promoted to ints during arithmetic,
    // so the b1 << 8 doesn't end up as zero before being or-ed with b2.
    // And the left-shift operator has higher precedence than the or operator.
    return b1 << 8 | b2;
  }

  uint32_t DecodeUInt24() {
    DCHECK_LE(3u, Remaining());
    const uint8_t b1 = DecodeUInt8();
    const uint8_t b2 = DecodeUInt8();
    const uint8_t b3 = DecodeUInt8();
    return b1 << 16 | b2 << 8 | b3;
  }

  // For 31-bit unsigned integers, where the 32nd bit is reserved for future
  // use (i.e. the high-bit of the first byte of the encoding); examples:
  // the Stream Id in a frame header or the Window Size Increment in a
  // WINDOW_UPDATE frame.
  uint32_t DecodeUInt31() {
    DCHECK_LE(4u, Remaining());
    const uint8_t b1 = DecodeUInt8() & 0x7f;  // Mask out the high order bit.
    const uint8_t b2 = DecodeUInt8();
    const uint8_t b3 = DecodeUInt8();
    const uint8_t b4 = DecodeUInt8();
    return b1 << 24 | b2 << 16 | b3 << 8 | b4;
  }

  uint32_t DecodeUInt32() {
    DCHECK_LE(4u, Remaining());
    const uint8_t b1 = DecodeUInt8();
    const uint8_t b2 = DecodeUInt8();
    const uint8_t b3 = DecodeUInt8();
    const uint8_t b4 = DecodeUInt8();
    return b1 << 24 | b2 << 16 | b3 << 8 | b4;
  }

  // We assume the decode buffers will typically be modest in size (i.e. often a
  // few KB, perhaps as high as 100KB). Let's make sure during testing that we
  // don't go very high, with 32MB selected rather arbitrarily.
  static constexpr size_t MaxQuicHttpDecodeBufferLength() { return 1 << 25; }

 protected:
#ifndef NDEBUG
  // These are part of validating during tests that there is at most one
  // QuicHttpDecodeBufferSubset instance at a time for any DecodeBuffer
  // instance.
  void set_subset_of_base(QuicHttpDecodeBuffer* base,
                          const QuicHttpDecodeBufferSubset* subset);
  void clear_subset_of_base(QuicHttpDecodeBuffer* base,
                            const QuicHttpDecodeBufferSubset* subset);
#endif

 private:
#ifndef NDEBUG
  void set_subset(const QuicHttpDecodeBufferSubset* subset);
  void clear_subset(const QuicHttpDecodeBufferSubset* subset);
#endif

  // Prevent heap allocation of QuicHttpDecodeBuffer.
  static void* operator new(size_t s);
  static void* operator new[](size_t s);
  static void operator delete(void* p);
  static void operator delete[](void* p);

  const char* const buffer_;
  const char* cursor_;
  const char* const beyond_;
  const QuicHttpDecodeBufferSubset* subset_ = nullptr;  // Used for DCHECKs.

  DISALLOW_COPY_AND_ASSIGN(QuicHttpDecodeBuffer);
};

// QuicHttpDecodeBufferSubset is used when decoding a known sized chunk of data,
// which starts at base->cursor(), and continues for subset_len, which may be
// entirely in |base|, or may extend beyond it (hence the MinLengthRemaining
// in the constructor).
// There are two benefits to using QuicHttpDecodeBufferSubset: it ensures that
// the cursor of |base| is advanced when the subset's destructor runs, and it
// ensures that the consumer of the subset can't go beyond the subset which
// it is intended to decode.
// There must be only a single QuicHttpDecodeBufferSubset at a time for a base
// QuicHttpDecodeBuffer, though they can be nested (i.e. a DecodeBufferSubset's
// base may itself be a QuicHttpDecodeBufferSubset). This avoids the
// AdvanceCursor being called erroneously.
class QUIC_EXPORT_PRIVATE QuicHttpDecodeBufferSubset
    : public QuicHttpDecodeBuffer {
 public:
  QuicHttpDecodeBufferSubset(QuicHttpDecodeBuffer* base, size_t subset_len)
      : QuicHttpDecodeBuffer(base->cursor(),
                             base->MinLengthRemaining(subset_len)),
        base_buffer_(base) {
#ifndef NDEBUG
    DebugSetup();
#endif
  }

  ~QuicHttpDecodeBufferSubset() {
    size_t offset = Offset();
#ifndef NDEBUG
    DebugTearDown();
#endif
    base_buffer_->AdvanceCursor(offset);
  }

 private:
  QuicHttpDecodeBuffer* const base_buffer_;
#ifndef NDEBUG
  size_t start_base_offset_;  // Used for DCHECKs.
  size_t max_base_offset_;    // Used for DCHECKs.

  void DebugSetup();
  void DebugTearDown();
#endif

  DISALLOW_COPY_AND_ASSIGN(QuicHttpDecodeBufferSubset);
};

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_QUIC_HTTP_DECODE_BUFFER_H_
