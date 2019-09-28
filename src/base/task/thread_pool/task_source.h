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
  kJob,
  kMax = kJob,
};

struct BASE_EXPORT ExecutionEnvironment {
  SequenceToken token;
  SequenceLocalStorageMap* sequence_local_storage;
};

// A TaskSource is a virtual class that provides a series of Tasks that must be
// executed.
//
// In order to execute a task from this TaskSource, a worker should first make
// sure that a task can run with WillRunTask() which returns a RunIntent.
// TakeTask() can then be called to access the next Task, and DidProcessTask()
// must be called after the task was processed. Many overlapping chains of
// WillRunTask(), TakeTask(), run and DidProcessTask() can run concurrently, as
// permitted by WillRunTask(). This ensure that the number of workers
// concurrently running tasks never go over the intended concurrency.
//
// In comments below, an "empty TaskSource" is a TaskSource with no Task.
//
// Note: there is a known refcounted-ownership cycle in the Scheduler
// architecture: TaskSource -> TaskRunner -> TaskSource -> ... This is okay so
// long as the other owners of TaskSource (PriorityQueue and WorkerThread in
// alternation and ThreadGroupImpl::WorkerThreadDelegateImpl::GetWork()
// temporarily) keep running it (and taking Tasks from it as a result). A
// dangling reference cycle would only occur should they release their reference
// to it while it's not empty. In other words, it is only correct for them to
// release it when DidProcessTask() returns false.
//
// This class is thread-safe.
class BASE_EXPORT TaskSource : public RefCountedThreadSafe<TaskSource> {
 protected:
  // Indicates whether a TaskSource has reached its maximum intended concurrency
  // and may not run any additional tasks.
  enum class Saturated {
    kYes,
    kNo,
  };

 public:
  // Indicates if a task was run or skipped as a result of shutdown.
  enum class RunResult {
    kDidRun,
    kSkippedAtShutdown,
  };

  // Result of WillRunTask(). A single task associated with a RunIntent may be
  // accessed with TakeTask() and run iff this evaluates to true.
  class BASE_EXPORT RunIntent {
   public:
    RunIntent() = default;
    RunIntent(RunIntent&&) noexcept;
    ~RunIntent();

    RunIntent& operator=(RunIntent&&);

    operator bool() const { return !!task_source_; }

    // Returns true iff the TaskSource from which this RunIntent was obtained
    // may not run any additional tasks beyond this RunIntent as it has reached
    // its maximum concurrency. This indicates that the TaskSource no longer
    // needs to be queued.
    bool IsSaturated() const { return is_saturated_ == Saturated::kYes; }

    const TaskSource* task_source() const { return task_source_; }

    void ReleaseForTesting() {
      DCHECK(task_source_);
      task_source_ = nullptr;
    }

   private:
    friend class TaskSource;

    // Indicates the step of a run intent chain.
    enum class State {
      kInitial,       // After WillRunTask().
      kTaskAcquired,  // After TakeTask().
      kCompleted,     // After DidProcessTask().
    };

    RunIntent(const TaskSource* task_source, Saturated is_saturated);

    void Release() {
      DCHECK_EQ(run_step_, State::kCompleted);
      DCHECK(task_source_);
      task_source_ = nullptr;
    }

    const TaskSource* task_source_ = nullptr;
    State run_step_ = State::kInitial;
    Saturated is_saturated_ = Saturated::kYes;
  };

  // A Transaction can perform multiple operations atomically on a
  // TaskSource. While a Transaction is alive, it is guaranteed that nothing
  // else will access the TaskSource; the TaskSource's lock is held for the
  // lifetime of the Transaction.
  class BASE_EXPORT Transaction {
   public:
    Transaction(Transaction&& other);
    ~Transaction();

    operator bool() const { return !!task_source_; }

    // Returns the next task to run from this TaskSource. This should be called
    // only with a valid |intent|. Cannot be called on an empty TaskSource.
    //
    // Because this method cannot be called on an empty TaskSource, the returned
    // Optional<Task> is never nullptr. An Optional is used in preparation for
    // the merge between ThreadPool and TaskQueueManager (in Blink).
    // https://crbug.com/783309
    Optional<Task> TakeTask(RunIntent* intent) WARN_UNUSED_RESULT;

    // Must be called once the task was run or skipped. |run_result| indicates
    // if the task executed. Cannot be called on an empty TaskSource. Returns
    // true if the TaskSource should be queued after this operation.
    bool DidProcessTask(RunIntent intent,
                        RunResult run_result = RunResult::kDidRun);

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

  // Informs this TaskSource that an additional Task could be run. Returns a
  // RunIntent that evaluates to true if this operation is allowed (TakeTask()
  // can be called), or false otherwise. This function is not thread safe and
  // must be externally synchronized (e.g. by the lock of the PriorityQueue
  // holding the TaskSource).
  virtual RunIntent WillRunTask() = 0;

  // Thread-safe but the returned value may immediately be obsolete. As such
  // this should only be used as a best-effort guess of how many more workers
  // are needed.
  virtual size_t GetRemainingConcurrency() const = 0;

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
  // DidProcessTask() returns false, guaranteeing it is safe to dereference this
  // pointer. Otherwise, the caller should guarantee such TaskRunner still
  // exists before dereferencing.
  TaskRunner* task_runner() const { return task_runner_; }

  TaskSourceExecutionMode execution_mode() const { return execution_mode_; }

 protected:
  virtual ~TaskSource();

  virtual Optional<Task> TakeTask() = 0;

  // Informs this TaskSource that a task was processed. |was_run| indicates
  // whether the task executed or not. Returns true if the TaskSource
  // should be queued after this operation.
  virtual bool DidProcessTask(RunResult run_result) = 0;

  virtual SequenceSortKey GetSortKey() const = 0;

  virtual void Clear() = 0;

  // Sets TaskSource priority to |priority|.
  void UpdatePriority(TaskPriority priority);

  // Constructs and returns a RunIntent, where |is_saturated| indicates that the
  // TaskSource has reached its maximum concurrency.
  RunIntent MakeRunIntent(Saturated is_saturated) const;

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
  // DidProcessTask() returns false.
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
  RegisteredTaskSource(RegisteredTaskSource&& other) noexcept;
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

// Base implementation for TransactionWith[Owned/Registered]TaskSource (with
// Transaction as the decorator) and RunIntentWithRegisteredTaskSource (with
// RunIntent as the decorator).
template <class Decorator, class T>
class BASE_EXPORT DecoratorWithTaskSource : public Decorator {
 public:
  DecoratorWithTaskSource() = default;
  DecoratorWithTaskSource(std::nullptr_t) : DecoratorWithTaskSource() {}
  DecoratorWithTaskSource(T task_source_in, Decorator decorator)
      : Decorator(std::move(decorator)),
        task_source_(std::move(task_source_in)) {
    DCHECK_EQ(task_source_.get(), this->task_source());
  }
  DecoratorWithTaskSource(DecoratorWithTaskSource&& other) = default;
  ~DecoratorWithTaskSource() = default;

  DecoratorWithTaskSource& operator=(DecoratorWithTaskSource&&) = default;

  T take_task_source() { return std::move(task_source_); }

 protected:
  T task_source_;

  DISALLOW_COPY_AND_ASSIGN(DecoratorWithTaskSource);
};

// A RunIntent with an additional RegisteredTaskSource member.
using RunIntentWithRegisteredTaskSource =
    DecoratorWithTaskSource<TaskSource::RunIntent, RegisteredTaskSource>;

template <class T>
struct BASE_EXPORT BasicTransactionWithTaskSource
    : public DecoratorWithTaskSource<TaskSource::Transaction, T> {
  using DecoratorWithTaskSource<TaskSource::Transaction,
                                T>::DecoratorWithTaskSource;

  static BasicTransactionWithTaskSource FromTaskSource(T task_source) {
    auto transaction = task_source->BeginTransaction();
    return BasicTransactionWithTaskSource(std::move(task_source),
                                          std::move(transaction));
  }
};

// A Transaction with an additional scoped_refptr<TaskSource> member. Useful to
// carry ownership of a TaskSource with an associated Transaction.
using TransactionWithOwnedTaskSource =
    BasicTransactionWithTaskSource<scoped_refptr<TaskSource>>;

// A Transaction with an additional RegisteredTaskSource member. Useful to carry
// a RegisteredTaskSource with an associated Transaction.
using TransactionWithRegisteredTaskSource =
    BasicTransactionWithTaskSource<RegisteredTaskSource>;

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_THREAD_POOL_TASK_SOURCE_H_
