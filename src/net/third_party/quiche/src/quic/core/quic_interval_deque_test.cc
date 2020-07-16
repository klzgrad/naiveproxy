// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_interval_deque.h"
#include <cstdint>
#include <ostream>
#include "net/third_party/quiche/src/quic/core/quic_interval.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_interval_deque_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {
namespace {

const int32_t kSize = 100;
const std::size_t kIntervalStep = 10;

}  // namespace

struct TestIntervalItem {
  int32_t val;
  std::size_t interval_start, interval_end;
  QuicInterval<std::size_t> interval() const {
    return QuicInterval<std::size_t>(interval_start, interval_end);
  }
  TestIntervalItem(int32_t val,
                   std::size_t interval_start,
                   std::size_t interval_end)
      : val(val), interval_start(interval_start), interval_end(interval_end) {}
};

typedef QuicIntervalDeque<TestIntervalItem> QID;

class QuicIntervalDequeTest : public QuicTest {
 public:
  QuicIntervalDequeTest() {
    // Add items with intervals of |kIntervalStep| size.
    for (int32_t i = 0; i < kSize; ++i) {
      const std::size_t interval_begin = kIntervalStep * i;
      const std::size_t interval_end = interval_begin + kIntervalStep;
      qid_.PushBack(TestIntervalItem(i, interval_begin, interval_end));
    }
  }

  QID qid_;
};

// The goal of this test is to show insertion/push_back, iteration, and and
// deletion/pop_front from the container.
TEST_F(QuicIntervalDequeTest, InsertRemoveSize) {
  QID qid;

  EXPECT_EQ(qid.Size(), std::size_t(0));
  qid.PushBack(TestIntervalItem(0, 0, 10));
  EXPECT_EQ(qid.Size(), std::size_t(1));
  qid.PushBack(TestIntervalItem(1, 10, 20));
  EXPECT_EQ(qid.Size(), std::size_t(2));
  qid.PushBack(TestIntervalItem(2, 20, 30));
  EXPECT_EQ(qid.Size(), std::size_t(3));
  qid.PushBack(TestIntervalItem(3, 30, 40));
  EXPECT_EQ(qid.Size(), std::size_t(4));

  // Advance the index all the way...
  int32_t i = 0;
  for (auto it = qid.DataAt(0); it != qid.DataEnd(); ++it, ++i) {
    const int32_t index = QuicIntervalDequePeer::GetCachedIndex(&qid);
    EXPECT_EQ(index, i);
    EXPECT_EQ(it->val, i);
  }
  const int32_t index = QuicIntervalDequePeer::GetCachedIndex(&qid);
  EXPECT_EQ(index, -1);

  qid.PopFront();
  EXPECT_EQ(qid.Size(), std::size_t(3));
  qid.PopFront();
  EXPECT_EQ(qid.Size(), std::size_t(2));
  qid.PopFront();
  EXPECT_EQ(qid.Size(), std::size_t(1));
  qid.PopFront();
  EXPECT_EQ(qid.Size(), std::size_t(0));

  EXPECT_QUIC_BUG(qid.PopFront(), "Trying to pop from an empty container.");
}

// The goal of this test is to push data into the container at specific
// intervals and show how the |DataAt| method can move the |cached_index| as the
// iterator moves through the data.
TEST_F(QuicIntervalDequeTest, InsertIterateWhole) {
  // The write index should point to the beginning of the container.
  const int32_t cached_index = QuicIntervalDequePeer::GetCachedIndex(&qid_);
  EXPECT_EQ(cached_index, 0);

  auto it = qid_.DataBegin();
  auto end = qid_.DataEnd();
  for (int32_t i = 0; i < kSize; ++i, ++it) {
    EXPECT_EQ(it->val, i);
    const std::size_t current_iteraval_begin = i * kIntervalStep;
    // The |DataAt| method should find the correct interval.
    auto lookup = qid_.DataAt(current_iteraval_begin);
    EXPECT_EQ(i, lookup->val);
    // Make sure the index hasn't changed just from using |DataAt|
    const int32_t index_before = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    EXPECT_EQ(index_before, i);
    // This increment should move the index forward.
    lookup++;
    // Check that the index has changed.
    const int32_t index_after = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    const int32_t after_i = (i + 1) == kSize ? -1 : (i + 1);
    EXPECT_EQ(index_after, after_i);
    EXPECT_NE(it, end);
  }
}

// The goal of this test is to push data into the container at specific
// intervals and show how the |DataAt| method can move the |cached_index| using
// the off-by-one logic.
TEST_F(QuicIntervalDequeTest, OffByOne) {
  // The write index should point to the beginning of the container.
  const int32_t cached_index = QuicIntervalDequePeer::GetCachedIndex(&qid_);
  EXPECT_EQ(cached_index, 0);

  auto it = qid_.DataBegin();
  auto end = qid_.DataEnd();
  for (int32_t i = 0; i < kSize - 1; ++i, ++it) {
    EXPECT_EQ(it->val, i);
    const int32_t off_by_one_i = i + 1;
    const std::size_t current_iteraval_begin = off_by_one_i * kIntervalStep;
    // Make sure the index has changed just from using |DataAt|
    const int32_t index_before = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    EXPECT_EQ(index_before, i);
    // The |DataAt| method should find the correct interval.
    auto lookup = qid_.DataAt(current_iteraval_begin);
    EXPECT_EQ(off_by_one_i, lookup->val);
    // Check that the index has changed.
    const int32_t index_after = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    const int32_t after_i = off_by_one_i == kSize ? -1 : off_by_one_i;
    EXPECT_EQ(index_after, after_i);
    EXPECT_NE(it, end);
  }
}

// The goal of this test is to push data into the container at specific
// intervals and show modify the structure with a live iterator.
TEST_F(QuicIntervalDequeTest, IteratorInvalidation) {
  // The write index should point to the beginning of the container.
  const int32_t cached_index = QuicIntervalDequePeer::GetCachedIndex(&qid_);
  EXPECT_EQ(cached_index, 0);

  const std::size_t iteraval_begin = (kSize - 1) * kIntervalStep;
  auto lookup = qid_.DataAt(iteraval_begin);
  EXPECT_EQ((*lookup).val, (kSize - 1));
  qid_.PopFront();
  EXPECT_QUIC_BUG(lookup++, "Iterator out of bounds.");
  auto lookup_end = qid_.DataAt(iteraval_begin + kIntervalStep);
  EXPECT_EQ(lookup_end, qid_.DataEnd());
}

// The goal of this test is the same as |InsertIterateWhole| but to
// skip certain intervals and show the |cached_index| is updated properly.
TEST_F(QuicIntervalDequeTest, InsertIterateSkip) {
  // The write index should point to the beginning of the container.
  const int32_t cached_index = QuicIntervalDequePeer::GetCachedIndex(&qid_);
  EXPECT_EQ(cached_index, 0);

  const std::size_t step = 4;
  for (int32_t i = 0; i < kSize; i += 4) {
    if (i != 0) {
      const int32_t before_i = (i - (step - 1));
      EXPECT_EQ(QuicIntervalDequePeer::GetCachedIndex(&qid_), before_i);
    }
    const std::size_t current_iteraval_begin = i * kIntervalStep;
    // The |DataAt| method should find the correct interval.
    auto lookup = qid_.DataAt(current_iteraval_begin);
    EXPECT_EQ(i, lookup->val);
    // Make sure the index _has_ changed just from using |DataAt| since we're
    // skipping data.
    const int32_t index_before = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    EXPECT_EQ(index_before, i);
    // This increment should move the index forward.
    lookup++;
    // Check that the index has changed.
    const int32_t index_after = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    const int32_t after_i = (i + 1) == kSize ? -1 : (i + 1);
    EXPECT_EQ(index_after, after_i);
  }
}

// The goal of this test is the same as |InsertIterateWhole| but it has
// |PopFront| calls interleaved to show the |cached_index| updates correctly.
TEST_F(QuicIntervalDequeTest, InsertDeleteIterate) {
  // The write index should point to the beginning of the container.
  const int32_t index = QuicIntervalDequePeer::GetCachedIndex(&qid_);
  EXPECT_EQ(index, 0);

  std::size_t limit = 0;
  for (int32_t i = 0; limit < qid_.Size(); ++i, ++limit) {
    // Always point to the beginning of the container.
    auto it = qid_.DataBegin();
    EXPECT_EQ(it->val, i);

    // Get an iterator.
    const std::size_t current_iteraval_begin = i * kIntervalStep;
    auto lookup = qid_.DataAt(current_iteraval_begin);
    const int32_t index_before = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    // The index should always point to 0.
    EXPECT_EQ(index_before, 0);
    // This iterator increment should effect the index.
    lookup++;
    const int32_t index_after = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    EXPECT_EQ(index_after, 1);
    // Decrement the |temp_size| and pop from the front.
    qid_.PopFront();
    // Show the index has been updated to point to 0 again (from 1).
    const int32_t index_after_pop =
        QuicIntervalDequePeer::GetCachedIndex(&qid_);
    EXPECT_EQ(index_after_pop, 0);
  }
}

// The goal of this test is to move the index to the end and then add more data
// to show it can be reset to a valid index.
TEST_F(QuicIntervalDequeTest, InsertIterateInsert) {
  // The write index should point to the beginning of the container.
  const int32_t index = QuicIntervalDequePeer::GetCachedIndex(&qid_);
  EXPECT_EQ(index, 0);

  int32_t iterated_elements = 0;
  for (int32_t i = 0; i < kSize; ++i, ++iterated_elements) {
    // Get an iterator.
    const std::size_t current_iteraval_begin = i * kIntervalStep;
    auto lookup = qid_.DataAt(current_iteraval_begin);
    const int32_t index_before = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    // The index should always point to i.
    EXPECT_EQ(index_before, i);
    // This iterator increment should effect the index.
    lookup++;
    // Show the index has been updated to point to i + 1 or -1 if at the end.
    const int32_t index_after = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    const int32_t after_i = (i + 1) == kSize ? -1 : (i + 1);
    EXPECT_EQ(index_after, after_i);
  }
  const int32_t invalid_index = QuicIntervalDequePeer::GetCachedIndex(&qid_);
  EXPECT_EQ(invalid_index, -1);

  // Add more data to the container, making the index valid.
  const std::size_t offset = qid_.Size();
  for (int32_t i = 0; i < kSize; ++i) {
    const std::size_t interval_begin = offset + (kIntervalStep * i);
    const std::size_t interval_end = offset + interval_begin + kIntervalStep;
    qid_.PushBack(TestIntervalItem(i + offset, interval_begin, interval_end));
    const int32_t index_current = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    // Index should now be valid and equal to the size of the container before
    // adding more items to it.
    EXPECT_EQ(index_current, iterated_elements);
  }
  // Show the index is still valid and hasn't changed since the first iteration
  // of the loop.
  const int32_t index_after_add = QuicIntervalDequePeer::GetCachedIndex(&qid_);
  EXPECT_EQ(index_after_add, iterated_elements);

  // Iterate over all the data in the container and eventually reset the index
  // as we did before.
  for (int32_t i = 0; i < kSize; ++i, ++iterated_elements) {
    const std::size_t interval_begin = offset + (kIntervalStep * i);
    const int32_t index_current = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    EXPECT_EQ(index_current, iterated_elements);
    auto lookup = qid_.DataAt(interval_begin);
    const int32_t expected_value = i + offset;
    EXPECT_EQ(lookup->val, expected_value);
    lookup++;
    const int32_t after_inc =
        (iterated_elements + 1) == (kSize * 2) ? -1 : (iterated_elements + 1);
    const int32_t after_index = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    EXPECT_EQ(after_index, after_inc);
  }
  // Show the index is now invalid.
  const int32_t invalid_index_again =
      QuicIntervalDequePeer::GetCachedIndex(&qid_);
  EXPECT_EQ(invalid_index_again, -1);
}

// The goal of this test is to push data into the container at specific
// intervals and show how the |DataAt| can iterate over already scanned data.
TEST_F(QuicIntervalDequeTest, RescanData) {
  // The write index should point to the beginning of the container.
  const int32_t index = QuicIntervalDequePeer::GetCachedIndex(&qid_);
  EXPECT_EQ(index, 0);

  auto it = qid_.DataBegin();
  auto end = qid_.DataEnd();
  for (int32_t i = 0; i < kSize - 1; ++i, ++it) {
    EXPECT_EQ(it->val, i);
    const std::size_t current_iteraval_begin = i * kIntervalStep;
    // The |DataAt| method should find the correct interval.
    auto lookup = qid_.DataAt(current_iteraval_begin);
    EXPECT_EQ(i, lookup->val);
    // Make sure the index has changed just from using |DataAt|
    const int32_t cached_index_before =
        QuicIntervalDequePeer::GetCachedIndex(&qid_);
    EXPECT_EQ(cached_index_before, i);
    // Ensure the real index has changed just from using |DataAt| and the
    // off-by-one logic
    const int32_t index_before = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    const int32_t before_i = i;
    EXPECT_EQ(index_before, before_i);
    // This increment should move the cached index forward.
    lookup++;
    // Check that the cached index has moved foward.
    const int32_t cached_index_after =
        QuicIntervalDequePeer::GetCachedIndex(&qid_);
    const int32_t after_i = (i + 1);
    EXPECT_EQ(cached_index_after, after_i);
    EXPECT_NE(it, end);
  }

  // Iterate over items which have been consumed before.
  int32_t expected_index = static_cast<int32_t>(kSize - 1);
  for (int32_t i = 0; i < kSize - 1; ++i) {
    const std::size_t current_iteraval_begin = i * kIntervalStep;
    // The |DataAt| method should find the correct interval.
    auto lookup = qid_.DataAt(current_iteraval_begin);
    EXPECT_EQ(i, lookup->val);
    // This increment shouldn't move the index forward as the index is currently
    // ahead.
    lookup++;
    // Check that the index hasn't moved foward.
    const int32_t index_after = QuicIntervalDequePeer::GetCachedIndex(&qid_);
    EXPECT_EQ(index_after, expected_index);
    EXPECT_NE(it, end);
  }
}

// The goal of this test is to show that popping from an empty container is a
// bug.
TEST_F(QuicIntervalDequeTest, PopEmpty) {
  QID qid;
  EXPECT_TRUE(qid.Empty());
  EXPECT_QUIC_BUG(qid.PopFront(), "Trying to pop from an empty container.");
}

// The goal of this test is to show that adding a zero-sized interval is a bug.
TEST_F(QuicIntervalDequeTest, ZeroSizedInterval) {
  QID qid;
  EXPECT_QUIC_BUG(qid.PushBack(TestIntervalItem(0, 0, 0)),
                  "Trying to save empty interval to .");
}

// The goal of this test is to show that an iterator to an empty container
// returns |DataEnd|.
TEST_F(QuicIntervalDequeTest, IteratorEmpty) {
  QID qid;
  auto it = qid.DataAt(0);
  EXPECT_EQ(it, qid.DataEnd());
}

}  // namespace test
}  // namespace quic
