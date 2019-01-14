// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/priority_queue.h"

#include <cstddef>

#include "base/stl_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

typedef PriorityQueue<int>::Priority Priority;
constexpr Priority kPriorities[] = {2, 1, 2, 0, 4, 3, 1, 4, 0};
constexpr Priority kNumPriorities = 5;  // max(kPriorities) + 1
constexpr size_t kNumElements = base::size(kPriorities);
constexpr int kFirstMinOrder[kNumElements] = {3, 8, 1, 6, 0, 2, 5, 4, 7};
constexpr int kLastMaxOrderErase[kNumElements] = {7, 4, 5, 2, 0, 6, 1, 8, 3};
constexpr int kFirstMaxOrder[kNumElements] = {4, 7, 5, 0, 2, 1, 6, 3, 8};
constexpr int kLastMinOrder[kNumElements] = {8, 3, 6, 1, 2, 0, 5, 7, 4};

class PriorityQueueTest : public testing::Test {
 protected:
  PriorityQueueTest() : queue_(kNumPriorities) {}

  void SetUp() override {
    CheckEmpty();
    for (size_t i = 0; i < kNumElements; ++i) {
      EXPECT_EQ(i, queue_.size());
      pointers_[i] = queue_.Insert(static_cast<int>(i), kPriorities[i]);
      EXPECT_FALSE(queue_.empty());
    }
    EXPECT_EQ(kNumElements, queue_.size());
  }

  void CheckEmpty() {
    EXPECT_TRUE(queue_.empty());
    EXPECT_EQ(0u, queue_.size());
    EXPECT_TRUE(queue_.FirstMin().is_null());
    EXPECT_TRUE(queue_.LastMin().is_null());
    EXPECT_TRUE(queue_.FirstMax().is_null());
    EXPECT_TRUE(queue_.LastMax().is_null());
  }

  PriorityQueue<int> queue_;
  PriorityQueue<int>::Pointer pointers_[kNumElements];
};

TEST_F(PriorityQueueTest, AddAndClear) {
  for (size_t i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(kPriorities[i], pointers_[i].priority());
    EXPECT_EQ(static_cast<int>(i), pointers_[i].value());
  }
  queue_.Clear();
  CheckEmpty();
}

TEST_F(PriorityQueueTest, FirstMinOrder) {
  for (size_t i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(kNumElements - i, queue_.size());
    // Also check Equals.
    EXPECT_TRUE(queue_.FirstMin().Equals(pointers_[kFirstMinOrder[i]]));
    EXPECT_EQ(kFirstMinOrder[i], queue_.FirstMin().value());
    queue_.Erase(queue_.FirstMin());
  }
  CheckEmpty();
}

TEST_F(PriorityQueueTest, LastMinOrder) {
  for (size_t i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(kLastMinOrder[i], queue_.LastMin().value());
    queue_.Erase(queue_.LastMin());
  }
  CheckEmpty();
}

TEST_F(PriorityQueueTest, FirstMaxOrder) {
  PriorityQueue<int>::Pointer p = queue_.FirstMax();
  size_t i = 0;
  for (; !p.is_null() && i < kNumElements;
       p = queue_.GetNextTowardsLastMin(p), ++i) {
    EXPECT_EQ(kFirstMaxOrder[i], p.value());
  }
  EXPECT_TRUE(p.is_null());
  EXPECT_EQ(kNumElements, i);
  queue_.Clear();
  CheckEmpty();
}

TEST_F(PriorityQueueTest, GetNextTowardsLastMinAndErase) {
  PriorityQueue<int>::Pointer current = queue_.FirstMax();
  for (size_t i = 0; i < kNumElements; ++i) {
    EXPECT_FALSE(current.is_null());
    EXPECT_EQ(kFirstMaxOrder[i], current.value());
    PriorityQueue<int>::Pointer next = queue_.GetNextTowardsLastMin(current);
    queue_.Erase(current);
    current = next;
  }
  EXPECT_TRUE(current.is_null());
  CheckEmpty();
}

TEST_F(PriorityQueueTest, FirstMaxOrderErase) {
  for (size_t i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(kFirstMaxOrder[i], queue_.FirstMax().value());
    queue_.Erase(queue_.FirstMax());
  }
  CheckEmpty();
}

TEST_F(PriorityQueueTest, LastMaxOrderErase) {
  for (size_t i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(kLastMaxOrderErase[i], queue_.LastMax().value());
    queue_.Erase(queue_.LastMax());
  }
  CheckEmpty();
}

TEST_F(PriorityQueueTest, EraseFromMiddle) {
  queue_.Erase(pointers_[2]);
  queue_.Erase(pointers_[3]);

  const int expected_order[] = { 8, 1, 6, 0, 5, 4, 7 };

  for (const auto& value : expected_order) {
    EXPECT_EQ(value, queue_.FirstMin().value());
    queue_.Erase(queue_.FirstMin());
  }
  CheckEmpty();
}

TEST_F(PriorityQueueTest, InsertAtFront) {
  queue_.InsertAtFront(9, 2);
  queue_.InsertAtFront(10, 0);
  queue_.InsertAtFront(11, 1);
  queue_.InsertAtFront(12, 1);

  const int expected_order[] = { 10, 3, 8, 12, 11, 1, 6, 9, 0, 2, 5, 4, 7 };

  for (const auto& value : expected_order) {
    EXPECT_EQ(value, queue_.FirstMin().value());
    queue_.Erase(queue_.FirstMin());
  }
  CheckEmpty();
}

}  // namespace

}  // namespace net
