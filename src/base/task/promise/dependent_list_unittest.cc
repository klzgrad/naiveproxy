// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/promise/dependent_list.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {

TEST(DependentList, ConstructUnresolved) {
  DependentList list(DependentList::ConstructUnresolved{});
  DependentList::Node node;
  EXPECT_EQ(DependentList::InsertResult::SUCCESS, list.Insert(&node));
  EXPECT_FALSE(list.IsRejected());
  EXPECT_FALSE(list.IsCanceled());
  EXPECT_FALSE(list.IsResolved());
  EXPECT_FALSE(list.IsSettled());
}

TEST(DependentList, ConstructResolved) {
  DependentList list(DependentList::ConstructResolved{});
  DependentList::Node node;
  EXPECT_EQ(DependentList::InsertResult::FAIL_PROMISE_RESOLVED,
            list.Insert(&node));
  EXPECT_TRUE(list.IsResolved());
  EXPECT_FALSE(list.IsRejected());
  EXPECT_FALSE(list.IsCanceled());
  EXPECT_TRUE(list.IsSettled());
}

TEST(DependentList, ConstructRejected) {
  DependentList list(DependentList::ConstructRejected{});
  DependentList::Node node;
  EXPECT_EQ(DependentList::InsertResult::FAIL_PROMISE_REJECTED,
            list.Insert(&node));
  EXPECT_TRUE(list.IsRejected());
  EXPECT_FALSE(list.IsCanceled());
  EXPECT_FALSE(list.IsResolved());
  EXPECT_TRUE(list.IsSettled());
}

TEST(DependentList, ConsumeOnceForResolve) {
  DependentList list(DependentList::ConstructUnresolved{});
  DependentList::Node node1;
  DependentList::Node node2;
  DependentList::Node node3;
  EXPECT_EQ(DependentList::InsertResult::SUCCESS, list.Insert(&node1));
  EXPECT_EQ(DependentList::InsertResult::SUCCESS, list.Insert(&node2));
  EXPECT_EQ(DependentList::InsertResult::SUCCESS, list.Insert(&node3));

  EXPECT_FALSE(list.IsResolved());
  EXPECT_FALSE(list.IsSettled());
  DependentList::Node* result = list.ConsumeOnceForResolve();
  EXPECT_TRUE(list.IsResolved());
  EXPECT_FALSE(list.IsRejected());
  EXPECT_FALSE(list.IsCanceled());
  EXPECT_TRUE(list.IsSettled());

  EXPECT_EQ(&node3, result);
  EXPECT_EQ(&node2, result->next.load());
  EXPECT_EQ(&node1, result->next.load()->next.load());
  EXPECT_EQ(nullptr, result->next.load()->next.load()->next.load());

  // Can't insert any more nodes.
  DependentList::Node node4;
  EXPECT_EQ(DependentList::InsertResult::FAIL_PROMISE_RESOLVED,
            list.Insert(&node4));
}

TEST(DependentList, ConsumeOnceForReject) {
  DependentList list(DependentList::ConstructUnresolved{});
  DependentList::Node node1;
  DependentList::Node node2;
  DependentList::Node node3;
  EXPECT_EQ(DependentList::InsertResult::SUCCESS, list.Insert(&node1));
  EXPECT_EQ(DependentList::InsertResult::SUCCESS, list.Insert(&node2));
  EXPECT_EQ(DependentList::InsertResult::SUCCESS, list.Insert(&node3));

  EXPECT_FALSE(list.IsRejected());
  EXPECT_FALSE(list.IsSettled());
  DependentList::Node* result = list.ConsumeOnceForReject();
  EXPECT_TRUE(list.IsRejected());
  EXPECT_FALSE(list.IsResolved());
  EXPECT_FALSE(list.IsCanceled());
  EXPECT_TRUE(list.IsSettled());

  EXPECT_EQ(&node3, result);
  EXPECT_EQ(&node2, result->next.load());
  EXPECT_EQ(&node1, result->next.load()->next.load());
  EXPECT_EQ(nullptr, result->next.load()->next.load()->next.load());

  // Can't insert any more nodes.
  DependentList::Node node4;
  EXPECT_EQ(DependentList::InsertResult::FAIL_PROMISE_REJECTED,
            list.Insert(&node4));
}

TEST(DependentList, ConsumeOnceForCancel) {
  DependentList list(DependentList::ConstructUnresolved{});
  DependentList::Node node1;
  DependentList::Node node2;
  DependentList::Node node3;
  EXPECT_EQ(DependentList::InsertResult::SUCCESS, list.Insert(&node1));
  EXPECT_EQ(DependentList::InsertResult::SUCCESS, list.Insert(&node2));
  EXPECT_EQ(DependentList::InsertResult::SUCCESS, list.Insert(&node3));

  EXPECT_FALSE(list.IsCanceled());
  EXPECT_FALSE(list.IsSettled());
  DependentList::Node* result = list.ConsumeOnceForCancel();
  EXPECT_TRUE(list.IsCanceled());
  EXPECT_FALSE(list.IsResolved());
  EXPECT_FALSE(list.IsRejected());
  EXPECT_TRUE(list.IsSettled());

  EXPECT_EQ(&node3, result);
  EXPECT_EQ(&node2, result->next.load());
  EXPECT_EQ(&node1, result->next.load()->next.load());
  EXPECT_EQ(nullptr, result->next.load()->next.load()->next.load());

  // Can't insert any more nodes.
  DependentList::Node node4;
  EXPECT_EQ(DependentList::InsertResult::FAIL_PROMISE_CANCELED,
            list.Insert(&node4));
}

}  // namespace internal
}  // namespace base
