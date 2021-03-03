// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spdy/platform/api/spdy_mem_slice.h"

#include <utility>

#include "common/platform/api/quiche_test.h"

namespace spdy {
namespace test {
namespace {

class SpdyMemSliceTest : public QuicheTest {
 public:
  SpdyMemSliceTest() {
    slice_ = SpdyMemSlice(1024);
    orig_data_ = slice_.data();
    orig_length_ = slice_.length();
  }

  SpdyMemSlice slice_;
  const char* orig_data_;
  size_t orig_length_;
};

TEST_F(SpdyMemSliceTest, MoveConstruct) {
  SpdyMemSlice moved(std::move(slice_));
  EXPECT_EQ(moved.data(), orig_data_);
  EXPECT_EQ(moved.length(), orig_length_);
  EXPECT_EQ(nullptr, slice_.data());
  EXPECT_EQ(0u, slice_.length());
}

TEST_F(SpdyMemSliceTest, MoveAssign) {
  SpdyMemSlice moved;
  moved = std::move(slice_);
  EXPECT_EQ(moved.data(), orig_data_);
  EXPECT_EQ(moved.length(), orig_length_);
  EXPECT_EQ(nullptr, slice_.data());
  EXPECT_EQ(0u, slice_.length());
}

}  // namespace
}  // namespace test
}  // namespace spdy
