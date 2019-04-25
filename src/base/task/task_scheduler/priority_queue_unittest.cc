// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/priority_queue.h"

#include <memory>
#include <utility>

#include "base/bind_helpers.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/task/task_scheduler/sequence.h"
#include "base/task/task_scheduler/task.h"
#include "base/task/task_traits.h"
#include "base/test/gtest_util.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {

namespace {

scoped_refptr<Sequence> MakeSequenceWithTraitsAndTask(
    const TaskTraits& traits) {
  scoped_refptr<Sequence> sequence = MakeRefCounted<Sequence>(traits);
  sequence->BeginTransaction().PushTask(
      Task(FROM_HERE, DoNothing(), TimeDelta()));
  return sequence;
}

class TaskSchedulerPriorityQueueWithSequencesTest : public testing::Test {
 protected:
  void ExpectNumSequences(size_t num_best_effort,
                          size_t num_user_visible,
                          size_t num_user_blocking) {
    EXPECT_EQ(pq.GetNumSequencesWithPriority(TaskPriority::BEST_EFFORT),
              num_best_effort);
    EXPECT_EQ(pq.GetNumSequencesWithPriority(TaskPriority::USER_VISIBLE),
              num_user_visible);
    EXPECT_EQ(pq.GetNumSequencesWithPriority(TaskPriority::USER_BLOCKING),
              num_user_blocking);
  }

  scoped_refptr<Sequence> sequence_a =
      MakeSequenceWithTraitsAndTask(TaskTraits(TaskPriority::USER_VISIBLE));
  SequenceSortKey sort_key_a = sequence_a->BeginTransaction().GetSortKey();

  scoped_refptr<Sequence> sequence_b =
      MakeSequenceWithTraitsAndTask(TaskTraits(TaskPriority::USER_BLOCKING));
  SequenceSortKey sort_key_b = sequence_b->BeginTransaction().GetSortKey();

  scoped_refptr<Sequence> sequence_c =
      MakeSequenceWithTraitsAndTask(TaskTraits(TaskPriority::USER_BLOCKING));
  SequenceSortKey sort_key_c = sequence_c->BeginTransaction().GetSortKey();

  scoped_refptr<Sequence> sequence_d =
      MakeSequenceWithTraitsAndTask(TaskTraits(TaskPriority::BEST_EFFORT));
  SequenceSortKey sort_key_d = sequence_d->BeginTransaction().GetSortKey();

  PriorityQueue pq;
};

}  // namespace

TEST_F(TaskSchedulerPriorityQueueWithSequencesTest, PushPopPeek) {
  EXPECT_TRUE(pq.IsEmpty());
  ExpectNumSequences(0U, 0U, 0U);

  // Push |sequence_a| in the PriorityQueue. It becomes the sequence with the
  // highest priority.
  pq.Push(sequence_a, sort_key_a);
  EXPECT_EQ(sort_key_a, pq.PeekSortKey());
  ExpectNumSequences(0U, 1U, 0U);

  // Push |sequence_b| in the PriorityQueue. It becomes the sequence with the
  // highest priority.
  pq.Push(sequence_b, sort_key_b);
  EXPECT_EQ(sort_key_b, pq.PeekSortKey());
  ExpectNumSequences(0U, 1U, 1U);

  // Push |sequence_c| in the PriorityQueue. |sequence_b| is still the sequence
  // with the highest priority.
  pq.Push(sequence_c, sort_key_c);
  EXPECT_EQ(sort_key_b, pq.PeekSortKey());
  ExpectNumSequences(0U, 1U, 2U);

  // Push |sequence_d| in the PriorityQueue. |sequence_b| is still the sequence
  // with the highest priority.
  pq.Push(sequence_d, sort_key_d);
  EXPECT_EQ(sort_key_b, pq.PeekSortKey());
  ExpectNumSequences(1U, 1U, 2U);

  // Pop |sequence_b| from the PriorityQueue. |sequence_c| becomes the sequence
  // with the highest priority.
  EXPECT_EQ(sequence_b, pq.PopSequence());
  EXPECT_EQ(sort_key_c, pq.PeekSortKey());
  ExpectNumSequences(1U, 1U, 1U);

  // Pop |sequence_c| from the PriorityQueue. |sequence_a| becomes the sequence
  // with the highest priority.
  EXPECT_EQ(sequence_c, pq.PopSequence());
  EXPECT_EQ(sort_key_a, pq.PeekSortKey());
  ExpectNumSequences(1U, 1U, 0U);

  // Pop |sequence_a| from the PriorityQueue. |sequence_d| becomes the sequence
  // with the highest priority.
  EXPECT_EQ(sequence_a, pq.PopSequence());
  EXPECT_EQ(sort_key_d, pq.PeekSortKey());
  ExpectNumSequences(1U, 0U, 0U);

  // Pop |sequence_d| from the PriorityQueue. It is now empty.
  EXPECT_EQ(sequence_d, pq.PopSequence());
  EXPECT_TRUE(pq.IsEmpty());
  ExpectNumSequences(0U, 0U, 0U);
}

TEST_F(TaskSchedulerPriorityQueueWithSequencesTest, RemoveSequence) {
  EXPECT_TRUE(pq.IsEmpty());

  // Push all test Sequences into the PriorityQueue. |sequence_b|
  // will be the sequence with the highest priority.
  pq.Push(sequence_a, sort_key_a);
  pq.Push(sequence_b, sort_key_b);
  pq.Push(sequence_c, sort_key_c);
  pq.Push(sequence_d, sort_key_d);
  EXPECT_EQ(sort_key_b, pq.PeekSortKey());
  ExpectNumSequences(1U, 1U, 2U);

  // Remove |sequence_a| from the PriorityQueue. |sequence_b| is still the
  // sequence with the highest priority.
  EXPECT_TRUE(pq.RemoveSequence(sequence_a));
  EXPECT_EQ(sort_key_b, pq.PeekSortKey());
  ExpectNumSequences(1U, 0U, 2U);

  // RemoveSequence() should return false if called on a sequence not in the
  // PriorityQueue.
  EXPECT_FALSE(pq.RemoveSequence(sequence_a));
  ExpectNumSequences(1U, 0U, 2U);

  // Remove |sequence_b| from the PriorityQueue. |sequence_c| becomes the
  // sequence with the highest priority.
  EXPECT_TRUE(pq.RemoveSequence(sequence_b));
  EXPECT_EQ(sort_key_c, pq.PeekSortKey());
  ExpectNumSequences(1U, 0U, 1U);

  // Remove |sequence_d| from the PriorityQueue. |sequence_c| is still the
  // sequence with the highest priority.
  EXPECT_TRUE(pq.RemoveSequence(sequence_d));
  EXPECT_EQ(sort_key_c, pq.PeekSortKey());
  ExpectNumSequences(0U, 0U, 1U);

  // Remove |sequence_c| from the PriorityQueue, making it empty.
  EXPECT_TRUE(pq.RemoveSequence(sequence_c));
  EXPECT_TRUE(pq.IsEmpty());
  ExpectNumSequences(0U, 0U, 0U);

  // Return false if RemoveSequence() is called on an empty PriorityQueue.
  EXPECT_FALSE(pq.RemoveSequence(sequence_c));
  ExpectNumSequences(0U, 0U, 0U);
}

TEST_F(TaskSchedulerPriorityQueueWithSequencesTest, UpdateSortKey) {
  EXPECT_TRUE(pq.IsEmpty());

  // Push all test Sequences into the PriorityQueue. |sequence_b| becomes the
  // sequence with the highest priority.
  pq.Push(sequence_a, sort_key_a);
  pq.Push(sequence_b, sort_key_b);
  pq.Push(sequence_c, sort_key_c);
  pq.Push(sequence_d, sort_key_d);
  EXPECT_EQ(sort_key_b, pq.PeekSortKey());
  ExpectNumSequences(1U, 1U, 2U);

  {
    // Downgrade |sequence_b| from USER_BLOCKING to BEST_EFFORT. |sequence_c|
    // (USER_BLOCKING priority) becomes the sequence with the highest priority.
    auto sequence_b_and_transaction =
        SequenceAndTransaction::FromSequence(sequence_b);
    sequence_b_and_transaction.transaction.UpdatePriority(
        TaskPriority::BEST_EFFORT);

    pq.UpdateSortKey(std::move(sequence_b_and_transaction));
    EXPECT_EQ(sort_key_c, pq.PeekSortKey());
    ExpectNumSequences(2U, 1U, 1U);
  }

  {
    // Update |sequence_c|'s sort key to one with the same priority.
    // |sequence_c| (USER_BLOCKING priority) is still the sequence with the
    // highest priority.
    auto sequence_c_and_transaction =
        SequenceAndTransaction::FromSequence(sequence_c);
    sequence_c_and_transaction.transaction.UpdatePriority(
        TaskPriority::USER_BLOCKING);

    pq.UpdateSortKey(std::move(sequence_c_and_transaction));
    ExpectNumSequences(2U, 1U, 1U);

    // Note: |sequence_c| is popped for comparison as |sort_key_c| becomes
    // obsolete. |sequence_a| (USER_VISIBLE priority) becomes the sequence with
    // the highest priority.
    EXPECT_EQ(sequence_c, pq.PopSequence());
    EXPECT_EQ(sort_key_a, pq.PeekSortKey());
    ExpectNumSequences(2U, 1U, 0U);
  }

  {
    // Upgrade |sequence_d| from BEST_EFFORT to USER_BLOCKING. |sequence_d|
    // becomes the sequence with the highest priority.
    auto sequence_d_and_transaction =
        SequenceAndTransaction::FromSequence(sequence_d);
    sequence_d_and_transaction.transaction.UpdatePriority(
        TaskPriority::USER_BLOCKING);

    pq.UpdateSortKey(std::move(sequence_d_and_transaction));
    ExpectNumSequences(1U, 1U, 1U);

    // Note: |sequence_d| is popped for comparison as |sort_key_d| becomes
    // obsolete.
    EXPECT_EQ(sequence_d, pq.PopSequence());
    // No-op if UpdateSortKey() is called on a Sequence not in the
    // PriorityQueue.
    EXPECT_EQ(sort_key_a, pq.PeekSortKey());
    ExpectNumSequences(1U, 1U, 0U);
  }

  {
    auto sequence_d_and_transaction =
        SequenceAndTransaction::FromSequence(sequence_d);

    pq.UpdateSortKey(std::move(sequence_d_and_transaction));
    ExpectNumSequences(1U, 1U, 0U);
    EXPECT_EQ(sequence_a, pq.PopSequence());
    ExpectNumSequences(1U, 0U, 0U);
    EXPECT_EQ(sequence_b, pq.PopSequence());
    ExpectNumSequences(0U, 0U, 0U);
  }

  {
    // No-op if UpdateSortKey() is called on an empty PriorityQueue.
    auto sequence_b_and_transaction =
        SequenceAndTransaction::FromSequence(sequence_b);
    pq.UpdateSortKey(std::move(sequence_b_and_transaction));
    EXPECT_TRUE(pq.IsEmpty());
    ExpectNumSequences(0U, 0U, 0U);
  }
}

}  // namespace internal
}  // namespace base
