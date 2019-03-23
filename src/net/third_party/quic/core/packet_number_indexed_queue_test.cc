// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/packet_number_indexed_queue.h"

#include <limits>
#include <map>

#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"

namespace quic {
namespace {

class PacketNumberIndexedQueueTest : public QuicTest {
 public:
  PacketNumberIndexedQueueTest() {}

 protected:
  PacketNumberIndexedQueue<QuicString> queue_;
};

TEST_F(PacketNumberIndexedQueueTest, InitialState) {
  EXPECT_TRUE(queue_.IsEmpty());
  EXPECT_EQ(0u, queue_.first_packet());
  EXPECT_EQ(0u, queue_.last_packet());
  EXPECT_EQ(0u, queue_.number_of_present_entries());
  EXPECT_EQ(0u, queue_.entry_slots_used());
}

TEST_F(PacketNumberIndexedQueueTest, InsertingContinuousElements) {
  ASSERT_TRUE(queue_.Emplace(1001, "one"));
  EXPECT_EQ("one", *queue_.GetEntry(1001));

  ASSERT_TRUE(queue_.Emplace(1002, "two"));
  EXPECT_EQ("two", *queue_.GetEntry(1002));

  EXPECT_FALSE(queue_.IsEmpty());
  EXPECT_EQ(1001u, queue_.first_packet());
  EXPECT_EQ(1002u, queue_.last_packet());
  EXPECT_EQ(2u, queue_.number_of_present_entries());
  EXPECT_EQ(2u, queue_.entry_slots_used());
}

TEST_F(PacketNumberIndexedQueueTest, InsertingOutOfOrder) {
  queue_.Emplace(1001, "one");

  ASSERT_TRUE(queue_.Emplace(1003, "three"));
  EXPECT_EQ(nullptr, queue_.GetEntry(1002));
  EXPECT_EQ("three", *queue_.GetEntry(1003));

  EXPECT_EQ(1001u, queue_.first_packet());
  EXPECT_EQ(1003u, queue_.last_packet());
  EXPECT_EQ(2u, queue_.number_of_present_entries());
  EXPECT_EQ(3u, queue_.entry_slots_used());

  ASSERT_FALSE(queue_.Emplace(1002, "two"));
}

TEST_F(PacketNumberIndexedQueueTest, InsertingIntoPast) {
  queue_.Emplace(1001, "one");
  EXPECT_FALSE(queue_.Emplace(1000, "zero"));
}

TEST_F(PacketNumberIndexedQueueTest, InsertingDuplicate) {
  queue_.Emplace(1001, "one");
  EXPECT_FALSE(queue_.Emplace(1001, "one"));
}

TEST_F(PacketNumberIndexedQueueTest, RemoveInTheMiddle) {
  queue_.Emplace(1001, "one");
  queue_.Emplace(1002, "two");
  queue_.Emplace(1003, "three");

  ASSERT_TRUE(queue_.Remove(1002));
  EXPECT_EQ(nullptr, queue_.GetEntry(1002));

  EXPECT_EQ(1001u, queue_.first_packet());
  EXPECT_EQ(1003u, queue_.last_packet());
  EXPECT_EQ(2u, queue_.number_of_present_entries());
  EXPECT_EQ(3u, queue_.entry_slots_used());

  EXPECT_FALSE(queue_.Emplace(1002, "two"));
  EXPECT_TRUE(queue_.Emplace(1004, "four"));
}

TEST_F(PacketNumberIndexedQueueTest, RemoveAtImmediateEdges) {
  queue_.Emplace(1001, "one");
  queue_.Emplace(1002, "two");
  queue_.Emplace(1003, "three");
  ASSERT_TRUE(queue_.Remove(1001));
  EXPECT_EQ(nullptr, queue_.GetEntry(1001));
  ASSERT_TRUE(queue_.Remove(1003));
  EXPECT_EQ(nullptr, queue_.GetEntry(1003));

  EXPECT_EQ(1002u, queue_.first_packet());
  EXPECT_EQ(1003u, queue_.last_packet());
  EXPECT_EQ(1u, queue_.number_of_present_entries());
  EXPECT_EQ(2u, queue_.entry_slots_used());

  EXPECT_TRUE(queue_.Emplace(1004, "four"));
}

TEST_F(PacketNumberIndexedQueueTest, RemoveAtDistantFront) {
  queue_.Emplace(1001, "one");
  queue_.Emplace(1002, "one (kinda)");
  queue_.Emplace(2001, "two");

  EXPECT_EQ(1001u, queue_.first_packet());
  EXPECT_EQ(2001u, queue_.last_packet());
  EXPECT_EQ(3u, queue_.number_of_present_entries());
  EXPECT_EQ(1001u, queue_.entry_slots_used());

  ASSERT_TRUE(queue_.Remove(1002));
  EXPECT_EQ(1001u, queue_.first_packet());
  EXPECT_EQ(2001u, queue_.last_packet());
  EXPECT_EQ(2u, queue_.number_of_present_entries());
  EXPECT_EQ(1001u, queue_.entry_slots_used());

  ASSERT_TRUE(queue_.Remove(1001));
  EXPECT_EQ(2001u, queue_.first_packet());
  EXPECT_EQ(2001u, queue_.last_packet());
  EXPECT_EQ(1u, queue_.number_of_present_entries());
  EXPECT_EQ(1u, queue_.entry_slots_used());
}

TEST_F(PacketNumberIndexedQueueTest, RemoveAtDistantBack) {
  queue_.Emplace(1001, "one");
  queue_.Emplace(2001, "two");

  EXPECT_EQ(1001u, queue_.first_packet());
  EXPECT_EQ(2001u, queue_.last_packet());

  ASSERT_TRUE(queue_.Remove(2001));
  EXPECT_EQ(1001u, queue_.first_packet());
  EXPECT_EQ(2001u, queue_.last_packet());
}

TEST_F(PacketNumberIndexedQueueTest, ClearAndRepopulate) {
  queue_.Emplace(1001, "one");
  queue_.Emplace(2001, "two");

  ASSERT_TRUE(queue_.Remove(1001));
  ASSERT_TRUE(queue_.Remove(2001));
  EXPECT_TRUE(queue_.IsEmpty());
  EXPECT_EQ(0u, queue_.first_packet());
  EXPECT_EQ(0u, queue_.last_packet());

  EXPECT_TRUE(queue_.Emplace(101, "one"));
  EXPECT_TRUE(queue_.Emplace(201, "two"));
  EXPECT_EQ(101u, queue_.first_packet());
  EXPECT_EQ(201u, queue_.last_packet());
}

TEST_F(PacketNumberIndexedQueueTest, FailToRemoveElementsThatNeverExisted) {
  ASSERT_FALSE(queue_.Remove(1000));
  queue_.Emplace(1001, "one");
  ASSERT_FALSE(queue_.Remove(1000));
  ASSERT_FALSE(queue_.Remove(1002));
}

TEST_F(PacketNumberIndexedQueueTest, FailToRemoveElementsTwice) {
  queue_.Emplace(1001, "one");
  ASSERT_TRUE(queue_.Remove(1001));
  ASSERT_FALSE(queue_.Remove(1001));
  ASSERT_FALSE(queue_.Remove(1001));
}

TEST_F(PacketNumberIndexedQueueTest, ConstGetter) {
  queue_.Emplace(1001, "one");
  const auto& const_queue = queue_;

  EXPECT_EQ("one", *const_queue.GetEntry(1001));
  EXPECT_EQ(nullptr, const_queue.GetEntry(1002));
}

}  // namespace
}  // namespace quic
