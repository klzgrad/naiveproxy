// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/task/common/intrusive_heap.h"
#include "base/task/common/intrusive_heap_lazy_staleness_policy.h"
#include "base/task/common/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {

using TestElementStalenessPolicy = LazyStalenessPolicy<test::TestElement>;

class IntrusiveHeapLazyStalenessPolicyTest : public testing::Test {
 protected:
  bool HeapIsNodeStaleAtPosition(
      IntrusiveHeap<test::TestElement, TestElementStalenessPolicy> heap,
      size_t position) {
    return heap.staleness_policy_.is_stale_[position];
  }
};

TEST_F(IntrusiveHeapLazyStalenessPolicyTest, LazyStalenessNoStaleElements) {
  IntrusiveHeap<test::TestElement, TestElementStalenessPolicy> heap;

  heap.insert({9, nullptr});
  heap.insert({10, nullptr});
  heap.insert({8, nullptr});
  heap.insert({2, nullptr});
  heap.insert({7, nullptr});
  heap.insert({15, nullptr});
  heap.insert({22, nullptr});
  heap.insert({3, nullptr});
  heap.insert({23, nullptr});

  for (size_t i = 1; i <= heap.size(); i++)
    EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, i));

  EXPECT_EQ(0u, heap.NumKnownStaleNodes());
}

TEST_F(IntrusiveHeapLazyStalenessPolicyTest, LazyStalenessClear) {
  IntrusiveHeap<test::TestElement, TestElementStalenessPolicy> heap;

  heap.insert({9, nullptr});
  heap.insert({10, nullptr});
  heap.insert({8, nullptr});
  heap.insert({2, nullptr});
  heap.insert({7, nullptr});
  heap.insert({15, nullptr});
  heap.insert({22, nullptr});

  size_t stale_positions[3] = {1, 2, 4};
  for (auto stale_position : stale_positions) {
    const_cast<test::TestElement*>(heap.begin() + stale_position - 1)->stale =
        true;
  }

  heap.insert({1, nullptr});
  heap.Clear();

  EXPECT_EQ(0u, heap.NumKnownStaleNodes());
  for (size_t i = 1; i <= heap.size(); i++)
    EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, i));
}

TEST_F(IntrusiveHeapLazyStalenessPolicyTest, LazyStalenessPop) {
  IntrusiveHeap<test::TestElement, TestElementStalenessPolicy> heap;

  heap.insert({9, nullptr});
  heap.insert({10, nullptr});
  heap.insert({8, nullptr});
  heap.insert({7, nullptr});
  heap.insert({15, nullptr});
  heap.insert({22, nullptr});
  heap.insert({2, nullptr, true});

  EXPECT_EQ(1u, heap.NumKnownStaleNodes());
  heap.Pop();
  EXPECT_EQ(0u, heap.NumKnownStaleNodes());

  for (size_t i = 1; i <= heap.size(); i++)
    EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, i));
}

TEST_F(IntrusiveHeapLazyStalenessPolicyTest, LazyStalenessInsert) {
  IntrusiveHeap<test::TestElement, TestElementStalenessPolicy> heap;

  heap.insert({9, nullptr});
  heap.insert({10, nullptr});
  heap.insert({8, nullptr});
  heap.insert({2, nullptr});
  heap.insert({7, nullptr});
  heap.insert({15, nullptr});
  heap.insert({22, nullptr});

  size_t stale_positions[4] = {1, 2, 4, 7};
  for (auto stale_position : stale_positions) {
    const_cast<test::TestElement*>(heap.begin() + stale_position - 1)->stale =
        true;
  }

  // Heap:
  //
  //       2
  //     /   \
  //    7     9
  //   /\     /\
  //  10 8  15  22
  //
  // 2, 7, 10, 22 are stale.

  heap.insert({1, nullptr});

  // Inserting 1 moves 2, 7 and 10. Hence, 2, 7 and 10 are known stale
  // nodes after the insertion. 22 has not been detected.

  size_t stale_count = 0;
  for (size_t i = 1; i <= heap.size(); i++) {
    bool stale_node = (heap.begin() + i - 1)->stale;
    bool marked_stale = HeapIsNodeStaleAtPosition(heap, i);
    if (!stale_node)
      EXPECT_FALSE(marked_stale);
    if (marked_stale)
      stale_count++;
  }

  EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, 1u));
  EXPECT_TRUE(HeapIsNodeStaleAtPosition(heap, 2u));
  EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, 3u));
  EXPECT_TRUE(HeapIsNodeStaleAtPosition(heap, 4u));
  EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, 5u));
  EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, 6u));
  EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, 7u));
  EXPECT_TRUE(HeapIsNodeStaleAtPosition(heap, 8u));
  EXPECT_EQ(3u, stale_count);
}

TEST_F(IntrusiveHeapLazyStalenessPolicyTest, LazyStalenessErase) {
  IntrusiveHeap<test::TestElement, TestElementStalenessPolicy> heap;
  HeapHandle index7;

  heap.insert({9, nullptr});
  heap.insert({10, nullptr});
  heap.insert({8, nullptr});
  heap.insert({2, nullptr});
  heap.insert({7, &index7});
  heap.insert({15, nullptr});
  heap.insert({22, nullptr});

  const_cast<test::TestElement&>(heap.at(index7)).stale = true;

  // Heap:
  //
  //       2
  //     /   \
  //    7     9
  //   /\     /\
  //  10 8  15  22
  //
  // 7 is stale, but not marked stale by the heap.

  heap.insert({1, nullptr});

  // Heap:
  //
  //         1
  //       /   \
  //      2     9
  //     /\     /\
  //    7  8  15  22
  //   /
  //  10
  //
  // 7 is a known stale node after insertion.

  EXPECT_EQ(1u, heap.NumKnownStaleNodes());

  heap.erase(index7);

  // Heap:
  //
  //        1
  //      /   \
  //     2     9
  //    /\     /\
  //  10  8  15  22
  //
  // No stale nodes remain in the heap.

  EXPECT_EQ(0u, heap.NumKnownStaleNodes());

  for (size_t i = 1; i <= heap.size(); i++)
    EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, i));
}

TEST_F(IntrusiveHeapLazyStalenessPolicyTest, LazyStalenessReplaceMinWithMin) {
  IntrusiveHeap<test::TestElement, TestElementStalenessPolicy> heap;

  heap.insert({9, nullptr});
  heap.insert({10, nullptr});
  heap.insert({8, nullptr});
  heap.insert({7, nullptr});
  heap.insert({15, nullptr});
  heap.insert({22, nullptr});
  heap.insert({2, nullptr, true});

  EXPECT_EQ(1u, heap.NumKnownStaleNodes());
  heap.ReplaceMin({1, nullptr});
  EXPECT_EQ(0u, heap.NumKnownStaleNodes());

  for (size_t i = 1; i <= heap.size(); i++)
    EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, i));
}

TEST_F(IntrusiveHeapLazyStalenessPolicyTest,
       LazyStalenessReplaceMinBubbleDown) {
  HeapHandle index7;
  IntrusiveHeap<test::TestElement, TestElementStalenessPolicy> heap;

  heap.insert({9, nullptr});
  heap.insert({10, nullptr});
  heap.insert({8, nullptr});
  heap.insert({7, &index7});
  heap.insert({15, nullptr});
  heap.insert({22, nullptr});
  heap.insert({2, nullptr, true});

  //  Heap:
  //
  //       2
  //     /   \
  //    8     7
  //   / \   / \
  //  10 15 22  9
  //
  //  2 is a known stale node.

  EXPECT_EQ(1u, heap.NumKnownStaleNodes());

  const_cast<test::TestElement&>(heap.at(index7)).stale = true;
  heap.ReplaceMin({23, nullptr});

  //  Heap:
  //
  //       7
  //     /   \
  //    8     9
  //   / \   / \
  //  10 15 22  23
  //
  //  7 is a known stale node, as it was detected when bubbling-down 23.

  EXPECT_EQ(1u, heap.NumKnownStaleNodes());

  EXPECT_TRUE(HeapIsNodeStaleAtPosition(heap, 1u));
  for (size_t i = 2; i <= heap.size(); i++)
    EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, i));
}

TEST_F(IntrusiveHeapLazyStalenessPolicyTest, LazyStalenessChangeKeyInPlace) {
  IntrusiveHeap<test::TestElement, TestElementStalenessPolicy> heap;
  HeapHandle index9;

  heap.insert({9, &index9, true});
  heap.insert({10, nullptr});
  heap.insert({8, nullptr});
  heap.insert({7, nullptr});
  heap.insert({15, nullptr});
  heap.insert({22, nullptr});
  heap.insert({2, nullptr});

  //  Heap:
  //
  //       2
  //     /   \
  //    8     7
  //   / \   / \
  //  10 15 22  9
  //
  //  9 is a known stale node.

  EXPECT_EQ(1u, heap.NumKnownStaleNodes());

  heap.ChangeKey(index9, {14, nullptr});

  // Heap:
  //
  //       2
  //     /   \
  //    8     7
  //   / \   / \
  //  10 15 22  14
  //
  // No stale nodes remain in the heap.

  EXPECT_EQ(0u, heap.NumKnownStaleNodes());

  for (size_t i = 1; i <= heap.size(); i++)
    EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, i));
}

TEST_F(IntrusiveHeapLazyStalenessPolicyTest, LazyStalenessChangeKeyBubbleDown) {
  HeapHandle index9;
  HeapHandle index15;
  IntrusiveHeap<test::TestElement, TestElementStalenessPolicy> heap;

  heap.insert({9, &index9, true});
  heap.insert({10, nullptr});
  heap.insert({8, nullptr});
  heap.insert({2, nullptr});
  heap.insert({7, nullptr});
  heap.insert({15, &index15});
  heap.insert({22, nullptr});

  // Heap:
  //
  //       2
  //     /   \
  //    7     9
  //   /\     /\
  //  10 8  15  22
  //
  // 9 is a known stale node.

  EXPECT_EQ(1u, heap.NumKnownStaleNodes());

  const_cast<test::TestElement&>(heap.at(index15)).stale = true;
  heap.ChangeKey(index9, {23, nullptr});

  // Heap:
  //
  //       2
  //     /   \
  //    7     15
  //   /\     /\
  //  10 8  23  22
  //
  // 15 is a known stale node, as it was detected when bubbling-down 23.

  EXPECT_EQ(1u, heap.NumKnownStaleNodes());

  for (size_t i = 1; i <= heap.size(); i++) {
    if (i == 3u)
      EXPECT_TRUE(HeapIsNodeStaleAtPosition(heap, i));
    else
      EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, i));
  }
}

TEST_F(IntrusiveHeapLazyStalenessPolicyTest, LazyStalenessChangeKeyBubbleUp) {
  HeapHandle index9;
  HeapHandle index2;
  IntrusiveHeap<test::TestElement, TestElementStalenessPolicy> heap;

  heap.insert({9, &index9, true});
  heap.insert({10, nullptr});
  heap.insert({8, nullptr});
  heap.insert({2, &index2});
  heap.insert({7, nullptr});
  heap.insert({15, nullptr});
  heap.insert({22, nullptr});

  // Heap:
  //
  //       2
  //     /   \
  //    7     9
  //   /\     /\
  //  10 8  15  22
  //
  // 9 is a known stale node.

  EXPECT_EQ(1u, heap.NumKnownStaleNodes());

  const_cast<test::TestElement&>(heap.at(index2)).stale = true;
  heap.ChangeKey(index9, {1, nullptr});

  // Heap:
  //
  //       1
  //     /   \
  //    7     2
  //   /\     /\
  //  10 8  15  22
  //
  // 2 is a known stale node, as it was detected when bubbling-up 1.

  EXPECT_EQ(1u, heap.NumKnownStaleNodes());

  for (size_t i = 1; i <= heap.size(); i++) {
    if (i == 3u)
      EXPECT_TRUE(HeapIsNodeStaleAtPosition(heap, i));
    else
      EXPECT_FALSE(HeapIsNodeStaleAtPosition(heap, i));
  }
}

}  // namespace internal
}  // namespace base
