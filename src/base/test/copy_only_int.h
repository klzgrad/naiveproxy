// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_COPY_ONLY_INT_H_
#define BASE_TEST_COPY_ONLY_INT_H_

#include "base/macros.h"

namespace base {

// A copy-only (not moveable) class that holds an integer. This is designed for
// testing containers. See also MoveOnlyInt.
class CopyOnlyInt {
 public:
  explicit CopyOnlyInt(int data = 1) : data_(data) {}
  CopyOnlyInt(const CopyOnlyInt& other) = default;
  ~CopyOnlyInt() { data_ = 0; }

  friend bool operator==(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return lhs.data_ == rhs.data_;
  }

  friend bool operator!=(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return !operator==(lhs, rhs);
  }

  friend bool operator<(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return lhs.data_ < rhs.data_;
  }

  friend bool operator>(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return rhs < lhs;
  }

  friend bool operator<=(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return !(rhs < lhs);
  }

  friend bool operator>=(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return !(lhs < rhs);
  }

  int data() const { return data_; }

 private:
  volatile int data_;

  CopyOnlyInt(CopyOnlyInt&&) = delete;
  CopyOnlyInt& operator=(CopyOnlyInt&) = delete;
};

}  // namespace base

#endif  // BASE_TEST_COPY_ONLY_INT_H_
