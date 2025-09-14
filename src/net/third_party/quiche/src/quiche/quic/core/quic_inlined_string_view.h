// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_INLINED_STRING_VIEW_H_
#define QUICHE_QUIC_CORE_QUIC_INLINED_STRING_VIEW_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "absl/numeric/bits.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

// QuicInlinedStringView<kSize> is a class that is similar to absl::string_view,
// with a notable distinction that it can inline up to `kSize - 1` characters
// (between 15 and 253 characters).
//
// Important use notes:
// - QuicInlinedStringView makes no assumptions about ownership of non-inlined
//   data; its primary purpose is to be a building block for other data
//   structures.
// - Unlike a regular string_view, the data pointer for QuicInlinedStringView
//   will start pointing to a different location if the string is inlined and
//   non-empty. For empty strings, the data pointer is always nullptr.
// - The string will be inlined iff its size is strictly below kSize; this is a
//   guaranteed API behavior.
template <size_t kSize>
class QUICHE_NO_EXPORT QuicInlinedStringView {
 public:
  // The largest size of a string that can be inlined by
  // `QuicInlinedStringView<kSize>`.
  static constexpr size_t kMaxInlinedSize = kSize - 1;
  static constexpr size_t kBufferSize = kSize;

  static_assert(kSize >= 16);
  static_assert(kSize <= 254);

  QuicInlinedStringView() { clear(); }
  explicit QuicInlinedStringView(absl::string_view source) {
    // Special-case empty strings, since memcpy has weird edge case behaviors
    // around null pointers.
    if (source.empty()) {
      clear();
      return;
    }

    QUICHE_DCHECK_EQ(source.size() & (~kLengthMask), size_t{0});
    if (source.size() > kMaxInlinedSize) {
      ViewRep rep;
      rep.data = source.data();
      rep.size = source.size();
      memcpy(&data_, &rep, sizeof(rep));
      set_last_byte(kNotInlinedMarker);
      return;
    }
    memcpy(data_, source.data(), source.size());
    set_last_byte(source.size());
  }

  // QuicInlinedStringView is trivially copyable and assignable; see, however,
  // the warning above regarding the pointer stability.
  QuicInlinedStringView(const QuicInlinedStringView&) = default;
  QuicInlinedStringView(QuicInlinedStringView&&) = default;
  QuicInlinedStringView& operator=(const QuicInlinedStringView&) = default;
  QuicInlinedStringView& operator=(QuicInlinedStringView&&) = default;

  // Returns true if the string is inlined into the view.
  bool IsInlined() const { return last_byte() != kNotInlinedMarker; }

  // string_view-compatible API.
  const char* data() const {
    if (!IsInlined()) {
      return view_rep_data();
    }
    return last_byte() == 0 ? nullptr : data_;
  }
  size_t size() const {
    return IsInlined() ? last_byte() : (view_rep_size() & kLengthMask);
  }
  bool empty() const { return size() == 0; }

  absl::string_view view() const { return absl::string_view(data(), size()); }
  void clear() { set_last_byte(0); }

 private:
  // On 64-bit platforms, we want to support kSize of 16, so we take the top
  // byte of the length, and use it for inlining.  On 32-bit platforms, that
  // would limit us to 24-bit lengths, which is too short, so we just require
  // the length to not overlap with the last byte (by setting minimum size to 16
  // bytes), and no masking is necessary.
  static_assert(sizeof(size_t) == 4 || sizeof(size_t) == 8);
  // Here, we have to spell out kLengthMask in terms of numeric_limits, since
  // specifying the value directly would fail compile-time overflow check on
  // 32-bit platforms.
  static constexpr size_t kLengthMask =
      sizeof(size_t) > 4 ? ((std::numeric_limits<size_t>::max() << 8) >> 8)
                         : std::numeric_limits<size_t>::max();
  static constexpr size_t kNotInlinedMarker = 0xff;

#if defined(__x86_64__)
  static_assert(kLengthMask == 0x00ffffffffffffff);
#endif

  // Representation of the string view when it is not inlined.
  struct ViewRep {
    const char* data;
    size_t size;
  };
  static_assert(sizeof(ViewRep) <= kSize);
  static_assert(absl::endian::native == absl::endian::little);

  // Accessors for ViewRep; necessary to work around C++ strict aliasing
  // limitations.  Clang will turn this into direct field access at `-O1`.
  const char* view_rep_data() const {
    ViewRep rep;
    memcpy(&rep, data_, sizeof(rep));
    return rep.data;
  }
  size_t view_rep_size() const {
    ViewRep rep;
    memcpy(&rep, data_, sizeof(rep));
    return rep.size;
  }
  // Those casts should be valid as long as uint8_t is unsigned char.
  const uint8_t& last_byte() const {
    return *reinterpret_cast<const uint8_t*>(data_ + kSize - 1);
  }
  void set_last_byte(uint8_t byte) {
    *reinterpret_cast<uint8_t*>(data_ + kSize - 1) = byte;
  }

  // Internal representation: if the string is inlined, the last byte is the
  // length of the inlined string, and all of the preceding bytes are the
  // inlined string.  If the string is not inlined, the `ViewRep` is at the
  // front, and 0xff is at the end (on 64-bit platforms, those may overlap).
  alignas(ViewRep) char data_[kSize];
};

static_assert(std::is_trivially_destructible_v<QuicInlinedStringView<16>>);
static_assert(std::is_trivially_copyable_v<QuicInlinedStringView<16>>);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_INLINED_STRING_VIEW_H_
