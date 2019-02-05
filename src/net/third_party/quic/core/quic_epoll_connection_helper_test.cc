// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_epoll_connection_helper.h"

#include "net/third_party/quic/core/crypto/quic_random.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/test_tools/fake_epoll_server.h"

namespace quic {
namespace test {
namespace {

class QuicEpollConnectionHelperTest : public QuicTest {
 protected:
  QuicEpollConnectionHelperTest()
      : helper_(&epoll_server_, QuicAllocator::BUFFER_POOL) {}

  test::FakeEpollServer epoll_server_;
  QuicEpollConnectionHelper helper_;
};

TEST_F(QuicEpollConnectionHelperTest, GetClock) {
  const QuicClock* clock = helper_.GetClock();
  QuicTime start = clock->Now();

  QuicTime::Delta delta = QuicTime::Delta::FromMilliseconds(5);
  epoll_server_.AdvanceBy(delta.ToMicroseconds());

  EXPECT_EQ(start + delta, clock->Now());
}

TEST_F(QuicEpollConnectionHelperTest, GetRandomGenerator) {
  QuicRandom* random = helper_.GetRandomGenerator();
  EXPECT_EQ(QuicRandom::GetInstance(), random);
}

}  // namespace
}  // namespace test
}  // namespace quic
