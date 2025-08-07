// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_MEM_SLICE_H_
#define QUICHE_COMMON_QUICHE_MEM_SLICE_H_

#include <cstddef>
#include <memory>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

// QuicheMemSlice is a memory buffer with a type-erased deleter callback.
class QUICHE_EXPORT QuicheMemSlice {
 public:
  using ReleaseCallback = SingleUseCallback<void(absl::string_view)>;

  // Creates a QuicheMemSlice by allocating memory on heap and copying the
  // specified bytes.
  static QuicheMemSlice Copy(absl::string_view data);

  // Constructs a empty QuicheMemSlice with no underlying data.
  QuicheMemSlice() = default;

  // Constructs a QuicheMemSlice that takes ownership of |buffer|.  The length
  // of the |buffer| must not be zero.  To construct an empty QuicheMemSlice,
  // use the zero-argument constructor instead.
  explicit QuicheMemSlice(QuicheBuffer buffer);

  // Constructs a QuicheMemSlice that takes ownership of |buffer| allocated on
  // heap.  |length| must not be zero.
  QuicheMemSlice(std::unique_ptr<char[]> buffer, size_t length);

  // Constructs a QuicheMemSlice with a custom deleter callback.
  QuicheMemSlice(const char* buffer, size_t length,
                 ReleaseCallback done_callback);

  QuicheMemSlice(const QuicheMemSlice& other) = delete;
  QuicheMemSlice& operator=(const QuicheMemSlice& other) = delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicheMemSlice(QuicheMemSlice&& other);
  QuicheMemSlice& operator=(QuicheMemSlice&& other);

  ~QuicheMemSlice();

  // Release the underlying reference. Further access the memory will result in
  // undefined behavior.
  void Reset();

  // Returns a const char pointer to underlying data buffer.
  const char* data() const { return data_; }
  // Returns the length of underlying data buffer.
  size_t length() const { return size_; }
  // Returns the representation of the underlying data as a string view.
  absl::string_view AsStringView() const {
    return absl::string_view(data_, size_);
  }

  bool empty() const { return size_ == 0; }

 private:
  const char* data_ = nullptr;
  size_t size_ = 0;
  ReleaseCallback done_callback_ = nullptr;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_MEM_SLICE_H_
