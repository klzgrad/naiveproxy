// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

using ::testing::ElementsAre;

namespace quic {
namespace test {
namespace {

TEST(QuicInlinedVectorTest, Swap) {
  {
    // Inline to inline.
    QuicInlinedVector<int, 2> self({1, 2});
    QuicInlinedVector<int, 2> other({3});

    self.swap(other);

    EXPECT_THAT(self, ElementsAre(3));
    EXPECT_THAT(other, ElementsAre(1, 2));
  }

  {
    // Inline to out-of-line.
    QuicInlinedVector<int, 2> self({1, 2});
    QuicInlinedVector<int, 2> other({3, 4, 5, 6});

    self.swap(other);

    EXPECT_THAT(self, ElementsAre(3, 4, 5, 6));
    EXPECT_THAT(other, ElementsAre(1, 2));
  }

  {
    // Out-of-line to inline.
    QuicInlinedVector<int, 2> self({1, 2, 3});
    QuicInlinedVector<int, 2> other({4, 5});

    self.swap(other);

    EXPECT_THAT(self, ElementsAre(4, 5));
    EXPECT_THAT(other, ElementsAre(1, 2, 3));
  }

  {
    // Out-of-line to Out-of-line.
    QuicInlinedVector<int, 2> self({1, 2, 3});
    QuicInlinedVector<int, 2> other({4, 5, 6, 7});

    self.swap(other);

    EXPECT_THAT(self, ElementsAre(4, 5, 6, 7));
    EXPECT_THAT(other, ElementsAre(1, 2, 3));
  }
}

}  // namespace
}  // namespace test
}  // namespace quic
