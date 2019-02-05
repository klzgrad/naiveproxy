// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/priority_queue.h"

#include <memory>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/task_scheduler/sequence.h"
#include "base/task/task_scheduler/task.h"
#include "base/task/task_traits.h"
#include "base/test/gtest_util.h"
#include "base/threading/platform_thread.h"
#include "base/threading/simple_thread.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {

namespace {

class ThreadBeginningTransaction : public SimpleThread {
 public:
  explicit ThreadBeginningTransaction(PriorityQueue* priority_queue)
      : SimpleThread("ThreadBeginningTransaction"),
        priority_queue_(priority_queue) {}

  // SimpleThread:
  void Run() override {
    auto transaction = priority_queue_->BeginTransaction();
    transaction_began_.Signal();
  }

  void ExpectTransactionDoesNotBegin() {
    // After a few milliseconds, the call to BeginTransaction() should not have
    // returned.
    EXPECT_FALSE(
        transaction_began_.TimedWait(TimeDelta::FromMilliseconds(250)));
  }

 private:
  PriorityQueue* const priority_queue_;
  WaitableEvent transaction_began_;

  DISALLOW_COPY_AND_ASSIGN(ThreadBeginningTransaction);
};

scoped_refptr<Sequence> MakeSequenceWithTraitsAndTask(
    const TaskTraits& traits) {
  scoped_refptr<Sequence> sequence = MakeRefCounted<Sequence>(traits);
  sequence->BeginTransaction().PushTask(
      Task(FROM_HERE, DoNothing(), TimeDelta()));
  return sequence;
}

class TaskSchedulerPriorityQueueWithSequencesTest : public testing::Test {
 protected:
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
  auto transaction = pq.BeginTransaction();
  EXPECT_TRUE(transaction->IsEmpty());

  // Push |sequence_a| in the PriorityQueue. It becomes the sequence with the
  // highest priority.
  transaction->Push(sequence_a, sort_key_a);
  EXPECT_EQ(sort_key_a, transaction->PeekSortKey());

  // Push |sequence_b| in the PriorityQueue. It becomes the sequence with the
  // highest priority.
  transaction->Push(sequence_b, sort_key_b);
  EXPECT_EQ(sort_key_b, transaction->PeekSortKey());

  // Push |sequence_c| in the PriorityQueue. |sequence_b| is still the sequence
  // with the highest priority.
  transaction->Push(sequence_c, sort_key_c);
  EXPECT_EQ(sort_key_b, transaction->PeekSortKey());

  // Push |sequence_d| in the PriorityQueue. |sequence_b| is still the sequence
  // with the highest priority.
  transaction->Push(sequence_d, sort_key_d);
  EXPECT_EQ(sort_key_b, transaction->PeekSortKey());

  // Pop |sequence_b| from the PriorityQueue. |sequence_c| becomes the sequence
  // with the highest priority.
  EXPECT_EQ(sequence_b, transaction->PopSequence());
  EXPECT_EQ(sort_key_c, transaction->PeekSortKey());

  // Pop |sequence_c| from the PriorityQueue. |sequence_a| becomes the sequence
  // with the highest priority.
  EXPECT_EQ(sequence_c, transaction->PopSequence());
  EXPECT_EQ(sort_key_a, transaction->PeekSortKey());

  // Pop |sequence_a| from the PriorityQueue. |sequence_d| becomes the sequence
  // with the highest priority.
  EXPECT_EQ(sequence_a, transaction->PopSequence());
  EXPECT_EQ(sort_key_d, transaction->PeekSortKey());

  // Pop |sequence_d| from the PriorityQueue. It is now empty.
  EXPECT_EQ(sequence_d, transaction->PopSequence());
  EXPECT_TRUE(transaction->IsEmpty());
}

TEST_F(TaskSchedulerPriorityQueueWithSequencesTest, RemoveSequence) {
  auto transaction = pq.BeginTransaction();
  EXPECT_TRUE(transaction->IsEmpty());

  // Push all test Sequences into the PriorityQueue. |sequence_b|
  // will be the sequence with the highest priority.
  transaction->Push(sequence_a, sort_key_a);
  transaction->Push(sequence_b, sort_key_b);
  transaction->Push(sequence_c, sort_key_c);
  transaction->Push(sequence_d, sort_key_d);
  EXPECT_EQ(sort_key_b, transaction->PeekSortKey());

  // Remove |sequence_a| from the PriorityQueue. |sequence_b| is still the
  // sequence with the highest priority.
  EXPECT_TRUE(transaction->RemoveSequence(sequence_a));
  EXPECT_EQ(sort_key_b, transaction->PeekSortKey());

  // RemoveSequence() should return false if called on a sequence not in the
  // PriorityQueue.
  EXPECT_FALSE(transaction->RemoveSequence(sequence_a));

  // Remove |sequence_b| from the PriorityQueue. |sequence_c| becomes the
  // sequence with the highest priority.
  EXPECT_TRUE(transaction->RemoveSequence(sequence_b));
  EXPECT_EQ(sort_key_c, transaction->PeekSortKey());

  // Remove |sequence_d| from the PriorityQueue. |sequence_c| is still the
  // sequence with the highest priority.
  EXPECT_TRUE(transaction->RemoveSequence(sequence_d));
  EXPECT_EQ(sort_key_c, transaction->PeekSortKey());

  // Remove |sequence_c| from the PriorityQueue, making it empty.
  EXPECT_TRUE(transaction->RemoveSequence(sequence_c));
  EXPECT_TRUE(transaction->IsEmpty());

  // Return false if RemoveSequence() is called on an empty PriorityQueue.
  EXPECT_FALSE(transaction->RemoveSequence(sequence_c));
}

TEST_F(TaskSchedulerPriorityQueueWithSequencesTest, UpdateSortKey) {
  {
    auto transaction = pq.BeginTransaction();
    EXPECT_TRUE(transaction->IsEmpty());

    // Push all test Sequences into the PriorityQueue. |sequence_b|
    // becomes the sequence with the highest priority.
    transaction->Push(sequence_a, sort_key_a);
    transaction->Push(sequence_b, sort_key_b);
    transaction->Push(sequence_c, sort_key_c);
    transaction->Push(sequence_d, sort_key_d);
    EXPECT_EQ(sort_key_b, transaction->PeekSortKey());

    // End |transaction| to respect lock acquisition order (a Sequence lock
    // can't be taken after a PriorityQueue lock).
  }

  {
    // Downgrade |sequence_b| from USER_BLOCKING to BEST_EFFORT. |sequence_c|
    // (USER_BLOCKING priority) becomes the sequence with the highest priority.
    auto sequence_b_and_transaction =
        SequenceAndTransaction::FromSequence(sequence_b);
    sequence_b_and_transaction.transaction.UpdatePriority(
        TaskPriority::BEST_EFFORT);

    auto transaction = pq.BeginTransaction();
    transaction->UpdateSortKey(std::move(sequence_b_and_transaction));
    EXPECT_EQ(sort_key_c, transaction->PeekSortKey());
  }

  {
    // Update |sequence_c|'s sort key to one with the same priority.
    // |sequence_c| (USER_BLOCKING priority) is still the sequence with the
    // highest priority.
    auto sequence_c_and_transaction =
        SequenceAndTransaction::FromSequence(sequence_c);
    sequence_c_and_transaction.transaction.UpdatePriority(
        TaskPriority::USER_BLOCKING);

    auto transaction = pq.BeginTransaction();
    transaction->UpdateSortKey(std::move(sequence_c_and_transaction));

    // Note: |sequence_c| is popped for comparison as |sort_key_c| becomes
    // obsolete. |sequence_a| (USER_VISIBLE priority) becomes the sequence with
    // the highest priority.
    EXPECT_EQ(sequence_c, transaction->PopSequence());
    EXPECT_EQ(sort_key_a, transaction->PeekSortKey());
  }

  {
    // Upgrade |sequence_d| from BEST_EFFORT to USER_BLOCKING. |sequence_d|
    // becomes the sequence with the highest priority.
    auto sequence_d_and_transaction =
        SequenceAndTransaction::FromSequence(sequence_d);
    sequence_d_and_transaction.transaction.UpdatePriority(
        TaskPriority::USER_BLOCKING);

    auto transaction = pq.BeginTransaction();
    transaction->UpdateSortKey(std::move(sequence_d_and_transaction));

    // Note: |sequence_d| is popped for comparison as |sort_key_d| becomes
    // obsolete.
    EXPECT_EQ(sequence_d, transaction->PopSequence());
    // No-op if UpdateSortKey() is called on a Sequence not in the
    // PriorityQueue.
    EXPECT_EQ(sort_key_a, transaction->PeekSortKey());
  }

  {
    auto sequence_d_and_transaction =
        SequenceAndTransaction::FromSequence(sequence_d);

    auto transaction = pq.BeginTransaction();
    transaction->UpdateSortKey(std::move(sequence_d_and_transaction));
    EXPECT_EQ(sequence_a, transaction->PopSequence());
    EXPECT_EQ(sequence_b, transaction->PopSequence());
  }

  // No-op if UpdateSortKey() is called on an empty PriorityQueue.
  auto sequence_b_and_transaction =
      SequenceAndTransaction::FromSequence(sequence_b);
  auto transaction = pq.BeginTransaction();
  transaction->UpdateSortKey(std::move(sequence_b_and_transaction));
  EXPECT_TRUE(transaction->IsEmpty());
}

// Check that creating Transactions on the same thread for 2 unrelated
// PriorityQueues causes a crash.
TEST(TaskSchedulerPriorityQueueTest, IllegalTwoTransactionsSameThread) {
  PriorityQueue pq_a;
  PriorityQueue pq_b;

  EXPECT_DCHECK_DEATH({
    auto transaction_a = pq_a.BeginTransaction();
    auto transaction_b = pq_b.BeginTransaction();
  });
}

// Check that it is possible to begin multiple Transactions for the same
// PriorityQueue on different threads. The call to BeginTransaction() on the
// second thread should block until the Transaction has ended on the first
// thread.
TEST(TaskSchedulerPriorityQueueTest, TwoTransactionsTwoThreads) {
  PriorityQueue pq;

  // Call BeginTransaction() on this thread and keep the Transaction alive.
  auto transaction = pq.BeginTransaction();

  // Call BeginTransaction() on another thread.
  ThreadBeginningTransaction thread_beginning_transaction(&pq);
  thread_beginning_transaction.Start();

  // After a few milliseconds, the call to BeginTransaction() on the other
  // thread should not have returned.
  thread_beginning_transaction.ExpectTransactionDoesNotBegin();

  // End the Transaction on the current thread.
  transaction.reset();

  // The other thread should exit after its call to BeginTransaction() returns.
  thread_beginning_transaction.Join();
}

}  // namespace internal
}  // namespace base
