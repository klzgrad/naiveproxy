// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_THREAD_POOL_TASK_SOURCE_H_
#define BASE_TASK_THREAD_POOL_TASK_SOURCE_H_

#include <stddef.h>

#include "base/base_export.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/optional.h"
#include "base/sequence_token.h"
#include "base/task/common/checked_lock.h"
#include "base/task/common/intrusive_heap.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool/sequence_sort_key.h"
#include "base/task/thread_pool/task.h"
#include "base/threading/sequence_local_storage_map.h"

namespace base {
namespace internal {

class TaskTracker;

enum class TaskSourceExecutionMode {
  kParallel,
  kSequenced,
  kSingleThread,
  kMax = kSingleThread,
};

struct BASE_EXPORT ExecutionEnvironment {
  SequenceToken token;
  SequenceLocalStorageMap* sequence_local_storage;
};

// A TaskSource is a virtual class that provides a series of Tasks that must be
// executed.
//
// In order to execute a task from this TaskSource, TakeTask() can be called to
// access the next Task, and DidRunTask() must be called after the task executed
// and before accessing any subsequent Tasks. This ensure that the number of
// workers concurrently running tasks never go over the intended concurrency.
//
// In comments below, an "empty TaskSource" is a TaskSource with no Task.
//
// Note: there is a known refcounted-ownership cycle in the Scheduler
// architecture: TaskSource -> TaskRunner -> TaskSource -> ... This is
// okay so long as the other owners of TaskSource (PriorityQueue and
// WorkerThread in alternation and
// ThreadGroupImpl::WorkerThreadDelegateImpl::GetWork() temporarily) keep
// running it (and taking Tasks from it as a result). A dangling reference cycle
// would only occur should they release their reference to it while it's not
// empty. In other words, it is only correct for them to release it when
// DidRunTask() returns false.
//
// This class is thread-safe.
class BASE_EXPORT TaskSource : public RefCountedThreadSafe<TaskSource> {
 public:
  // A Transaction can perform multiple operations atomically on a
  // TaskSource. While a Transaction is alive, it is guaranteed that nothing
  // else will access the TaskSource; the TaskSource's lock is held for the
  // lifetime of the Transaction.
  class BASE_EXPORT Transaction {
   public:
    Transaction(Transaction&& other);
    ~Transaction();

    // Returns the next task to run from this TaskSource. This should be called
    // only if NeedsWorker returns true. Cannot be called on an empty
    // TaskSource.
    //
    // Because this method cannot be called on an empty TaskSource, the returned
    // Optional<Task> is never nullptr. An Optional is used in preparation for
    // the merge between ThreadPool and TaskQueueManager (in Blink).
    // https://crbug.com/783309
    Optional<Task> TakeTask();

    // Must be called once the task was executed. Cannot be called on an empty
    // TaskSource. Returns true if the TaskSource should be queued after this
    // operation.
    bool DidRunTask();

    // Returns a SequenceSortKey representing the priority of the TaskSource.
    // Cannot be called on an empty TaskSource.
    SequenceSortKey GetSortKey() const;

    // Sets TaskSource priority to |priority|.
    void UpdatePriority(TaskPriority priority);

    // Deletes all tasks contained in this TaskSource.
    void Clear();

    // Returns the traits of all Tasks in the TaskSource.
    TaskTraits traits() const { return task_source_->traits_; }

    TaskSource* task_source() const { return task_source_; }

   protected:
    explicit Transaction(TaskSource* task_source);

   private:
    friend class TaskSource;

    TaskSource* task_source_;

    DISALLOW_COPY_AND_ASSIGN(Transaction);
  };

  // |traits| is metadata that applies to all Tasks in the TaskSource.
  // |task_runner| is a reference to the TaskRunner feeding this TaskSource.
  // |task_runner| can be nullptr only for tasks with no TaskRunner, in which
  // case |execution_mode| must be kParallel. Otherwise, |execution_mode| is the
  // execution mode of |task_runner|.
  TaskSource(const TaskTraits& traits,
             TaskRunner* task_runner,
             TaskSourceExecutionMode execution_mode);

  // Begins a Transaction. This method cannot be called on a thread which has an
  // active TaskSource::Transaction.
  Transaction BeginTransaction() WARN_UNUSED_RESULT;

  virtual ExecutionEnvironment GetExecutionEnvironment() = 0;

  // Support for IntrusiveHeap.
  void SetHeapHandle(const HeapHandle& handle);
  void ClearHeapHandle();
  HeapHandle heap_handle() const { return heap_handle_; }

  // Returns the shutdown behavior of all Tasks in the TaskSource. Can be
  // accessed without a Transaction because it is never mutated.
  TaskShutdownBehavior shutdown_behavior() const {
    return traits_.shutdown_behavior();
  }

  // A reference to TaskRunner is only retained between PushTask() and when
  // DidRunTask() returns false, guaranteeing it is safe to dereference this
  // pointer. Otherwise, the caller should guarantee such TaskRunner still
  // exists before dereferencing.
  TaskRunner* task_runner() const { return task_runner_; }

  TaskSourceExecutionMode execution_mode() const { return execution_mode_; }

 protected:
  virtual ~TaskSource();

  virtual Optional<Task> TakeTask() = 0;

  // Returns true if the TaskSource should be queued after this
  // operation.
  virtual bool DidRunTask() = 0;

  virtual SequenceSortKey GetSortKey() const = 0;

  virtual void Clear() = 0;

  // Sets TaskSource priority to |priority|.
  void UpdatePriority(TaskPriority priority);

  // The TaskTraits of all Tasks in the TaskSource.
  TaskTraits traits_;

 private:
  friend class RefCountedThreadSafe<TaskSource>;

  // Synchronizes access to all members.
  mutable CheckedLock lock_{UniversalPredecessor()};

  // The TaskSource's position in its current PriorityQueue. Access is protected
  // by the PriorityQueue's lock.
  HeapHandle heap_handle_;

  // A pointer to the TaskRunner that posts to this TaskSource, if any. The
  // derived class is responsible for calling AddRef() when a TaskSource from
  // which no Task is executing becomes non-empty and Release() when
  // DidRunTask() returns false.
  TaskRunner* task_runner_;

  TaskSourceExecutionMode execution_mode_;

  DISALLOW_COPY_AND_ASSIGN(TaskSource);
};

// Wrapper around TaskSource to signify the intent to queue and run it. A
// RegisteredTaskSource can only be created with TaskTracker.
class BASE_EXPORT RegisteredTaskSource {
 public:
  RegisteredTaskSource();
  RegisteredTaskSource(std::nullptr_t);
  RegisteredTaskSource(RegisteredTaskSource&& other);
  ~RegisteredTaskSource();

  RegisteredTaskSource& operator=(RegisteredTaskSource&& other);

  operator bool() const { return task_source_ != nullptr; }

  TaskSource* operator->() const { return task_source_.get(); }
  TaskSource* get() const { return task_source_.get(); }

  static RegisteredTaskSource CreateForTesting(
      scoped_refptr<TaskSource> task_source,
      TaskTracker* task_tracker = nullptr);

  scoped_refptr<TaskSource> Unregister();

 private:
  friend class TaskTracker;

  RegisteredTaskSource(scoped_refptr<TaskSource> task_source,
                       TaskTracker* task_tracker);

  scoped_refptr<TaskSource> task_source_;
  TaskTracker* task_tracker_;

  DISALLOW_COPY_AND_ASSIGN(RegisteredTaskSource);
};

template <class T>
struct BASE_EXPORT BasicTaskSourceAndTransaction {
  T task_source;
  TaskSource::Transaction transaction;

  static BasicTaskSourceAndTransaction FromTaskSource(T task_source) {
    auto transaction = task_source->BeginTransaction();
    return BasicTaskSourceAndTransaction(std::move(task_source),
                                         std::move(transaction));
  }

  BasicTaskSourceAndTransaction(T task_source_in,
                                TaskSource::Transaction transaction_in)
      : task_source(std::move(task_source_in)),
        transaction(std::move(transaction_in)) {}
  BasicTaskSourceAndTransaction(BasicTaskSourceAndTransaction&& other) =
      default;
  ~BasicTaskSourceAndTransaction() = default;

  DISALLOW_COPY_AND_ASSIGN(BasicTaskSourceAndTransaction);
};

using TaskSourceAndTransaction =
    BasicTaskSourceAndTransaction<scoped_refptr<TaskSource>>;
using RegisteredTaskSourceAndTransaction =
    BasicTaskSourceAndTransaction<RegisteredTaskSource>;

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_THREAD_POOL_TASK_SOURCE_H_
