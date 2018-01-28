// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_DECODER_DECODE_BUFFER_H_
#define NET_HTTP2_DECODER_DECODE_BUFFER_H_

// DecodeBuffer provides primitives for decoding various integer types found in
// HTTP/2 frames. It wraps a byte array from which we can read and decode
// serialized HTTP/2 frames, or parts thereof. DecodeBuffer is intended only for
// stack allocation, where the caller is typically going to use the DecodeBuffer
// instance as part of decoding the entire buffer before returning to its own
// caller.

#include <stddef.h>
#include <stdint.h>

#include <algorithm>

#include "base/logging.h"
#include "base/macros.h"
#include "net/http2/platform/api/http2_export.h"
#include "net/http2/platform/api/http2_string_piece.h"

namespace net {
class DecodeBufferSubset;

class HTTP2_EXPORT_PRIVATE DecodeBuffer {
 public:
  DecodeBuffer(const char* buffer, size_t len)
      : buffer_(buffer), cursor_(buffer), beyond_(buffer + len) {
    DCHECK(buffer != nullptr);
    // We assume the decode buffers will typically be modest in size (i.e. often
    // a few KB, perhaps as high as 100KB). Let's make sure during testing that
    // we don't go very high, with 32MB selected rather arbitrarily.
    const size_t kMaxDecodeBufferLength = 1 << 25;
    DCHECK_LE(len, kMaxDecodeBufferLength);
  }
  explicit DecodeBuffer(Http2StringPiece s)
      : DecodeBuffer(s.data(), s.size()) {}
  // Constructor for character arrays, typically in tests. For example:
  //    const char input[] = { 0x11 };
  //    DecodeBuffer b(input);
  template <size_t N>
  explicit DecodeBuffer(const char (&buf)[N]) : DecodeBuffer(buf, N) {}

  bool Empty() const { return cursor_ >= beyond_; }
  bool HasData() const { return cursor_ < beyond_; }
  size_t Remaining() const {
    DCHECK_LE(cursor_, beyond_);
    return beyond_ - cursor_;
  }
  size_t Offset() const { return cursor_ - buffer_; }
  size_t FullSize() const { return beyond_ - buffer_; }

  // Returns the minimum of the number of bytes remaining in this DecodeBuffer
  // and |length|, in support of determining how much of some structure/payload
  // is in this DecodeBuffer.
  size_t MinLengthRemaining(size_t length) const {
    return std::min(length, Remaining());
  }

  // For string decoding, returns a pointer to the next byte/char to be decoded.
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

  uint8_t DecodeUInt8();
  uint16_t DecodeUInt16();
  uint32_t DecodeUInt24();

  // For 31-bit unsigned integers, where the 32nd bit is reserved for future
  // use (i.e. the high-bit of the first byte of the encoding); examples:
  // the Stream Id in a frame header or the Window Size Increment in a
  // WINDOW_UPDATE frame.
  uint32_t DecodeUInt31();

  uint32_t DecodeUInt32();

 protected:
#ifndef NDEBUG
  // These are part of validating during tests that there is at most one
  // DecodeBufferSubset instance at a time for any DecodeBuffer instance.
  void set_subset_of_base(DecodeBuffer* base, const DecodeBufferSubset* subset);
  void clear_subset_of_base(DecodeBuffer* base,
                            const DecodeBufferSubset* subset);
#endif

 private:
#ifndef NDEBUG
  void set_subset(const DecodeBufferSubset* subset);
  void clear_subset(const DecodeBufferSubset* subset);
#endif

  // Prevent heap allocation of DecodeBuffer.
  static void* operator new(size_t s);
  static void* operator new[](size_t s);
  static void operator delete(void* p);
  static void operator delete[](void* p);

  const char* const buffer_;
  const char* cursor_;
  const char* const beyond_;
  const DecodeBufferSubset* subset_ = nullptr;  // Used for DCHECKs.

  DISALLOW_COPY_AND_ASSIGN(DecodeBuffer);
};

// DecodeBufferSubset is used when decoding a known sized chunk of data, which
// starts at base->cursor(), and continues for subset_len, which may be
// entirely in |base|, or may extend beyond it (hence the MinLengthRemaining
// in the constructor).
// There are two benefits to using DecodeBufferSubset: it ensures that the
// cursor of |base| is advanced when the subset's destructor runs, and it
// ensures that the consumer of the subset can't go beyond the subset which
// it is intended to decode.
// There must be only a single DecodeBufferSubset at a time for a base
// DecodeBuffer, though they can be nested (i.e. a DecodeBufferSubset's
// base may itself be a DecodeBufferSubset). This avoids the AdvanceCursor
// being called erroneously.
class HTTP2_EXPORT_PRIVATE DecodeBufferSubset : public DecodeBuffer {
 public:
  DecodeBufferSubset(DecodeBuffer* base, size_t subset_len)
      : DecodeBuffer(base->cursor(), base->MinLengthRemaining(subset_len)),
        base_buffer_(base) {
#ifndef NDEBUG
    DebugSetup();
#endif
  }

  ~DecodeBufferSubset() {
    size_t offset = Offset();
#ifndef NDEBUG
    DebugTearDown();
#endif
    base_buffer_->AdvanceCursor(offset);
  }

 private:
  DecodeBuffer* const base_buffer_;
#ifndef NDEBUG
  size_t start_base_offset_;  // Used for DCHECKs.
  size_t max_base_offset_;    // Used for DCHECKs.

  void DebugSetup();
  void DebugTearDown();
#endif

  DISALLOW_COPY_AND_ASSIGN(DecodeBufferSubset);
};

}  // namespace net

#endif  // NET_HTTP2_DECODER_DECODE_BUFFER_H_
