// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/thread_pool/task_tracker.h"

#include <atomic>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/debug/alias.h"
#include "base/json/json_writer.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/optional.h"
#include "base/sequence_token.h"
#include "base/synchronization/condition_variable.h"
#include "base/task/scoped_set_task_priority_for_current_thread.h"
#include "base/threading/sequence_local_storage_map.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"

namespace base {
namespace internal {

namespace {

constexpr const char* kExecutionModeString[] = {"parallel", "sequenced",
                                                "single thread"};
static_assert(
    size(kExecutionModeString) ==
        static_cast<size_t>(TaskSourceExecutionMode::kMax) + 1,
    "Array kExecutionModeString is out of sync with TaskSourceExecutionMode.");

// An immutable copy of a thread pool task's info required by tracing.
class TaskTracingInfo : public trace_event::ConvertableToTraceFormat {
 public:
  TaskTracingInfo(const TaskTraits& task_traits,
                  const char* execution_mode,
                  const SequenceToken& sequence_token)
      : task_traits_(task_traits),
        execution_mode_(execution_mode),
        sequence_token_(sequence_token) {}

  // trace_event::ConvertableToTraceFormat implementation.
  void AppendAsTraceFormat(std::string* out) const override;

 private:
  const TaskTraits task_traits_;
  const char* const execution_mode_;
  const SequenceToken sequence_token_;

  DISALLOW_COPY_AND_ASSIGN(TaskTracingInfo);
};

void TaskTracingInfo::AppendAsTraceFormat(std::string* out) const {
  DictionaryValue dict;

  dict.SetStringKey("task_priority",
                    base::TaskPriorityToString(task_traits_.priority()));
  dict.SetStringKey("execution_mode", execution_mode_);
  if (sequence_token_.IsValid())
    dict.SetIntKey("sequence_token", sequence_token_.ToInternalValue());

  std::string tmp;
  JSONWriter::Write(dict, &tmp);
  out->append(tmp);
}

// Constructs a histogram to track latency which is logging to
// "ThreadPool.{histogram_name}.{histogram_label}.{task_type_suffix}".
HistogramBase* GetLatencyHistogram(StringPiece histogram_name,
                                   StringPiece histogram_label,
                                   StringPiece task_type_suffix) {
  DCHECK(!histogram_name.empty());
  DCHECK(!histogram_label.empty());
  DCHECK(!task_type_suffix.empty());
  // Mimics the UMA_HISTOGRAM_HIGH_RESOLUTION_CUSTOM_TIMES macro. The minimums
  // and maximums were chosen to place the 1ms mark at around the 70% range
  // coverage for buckets giving us good info for tasks that have a latency
  // below 1ms (most of them) and enough info to assess how bad the latency is
  // for tasks that exceed this threshold.
  const std::string histogram = JoinString(
      {"ThreadPool", histogram_name, histogram_label, task_type_suffix}, ".");
  return Histogram::FactoryMicrosecondsTimeGet(
      histogram, TimeDelta::FromMicroseconds(1),
      TimeDelta::FromMilliseconds(20), 50,
      HistogramBase::kUmaTargetedHistogramFlag);
}

// Constructs a histogram to track task count which is logging to
// "ThreadPool.{histogram_name}.{histogram_label}.{task_type_suffix}".
HistogramBase* GetCountHistogram(StringPiece histogram_name,
                                 StringPiece histogram_label,
                                 StringPiece task_type_suffix) {
  DCHECK(!histogram_name.empty());
  DCHECK(!histogram_label.empty());
  DCHECK(!task_type_suffix.empty());
  // Mimics the UMA_HISTOGRAM_CUSTOM_COUNTS macro.
  const std::string histogram = JoinString(
      {"ThreadPool", histogram_name, histogram_label, task_type_suffix}, ".");
  // 500 was chosen as the maximum number of tasks run while queuing because
  // values this high would likely indicate an error, beyond which knowing the
  // actual number of tasks is not informative.
  return Histogram::FactoryGet(histogram, 1, 500, 50,
                               HistogramBase::kUmaTargetedHistogramFlag);
}

// Returns a histogram stored in a 2D array indexed by task priority and
// whether it may block.
// TODO(jessemckenna): use the STATIC_HISTOGRAM_POINTER_GROUP macro from
// histogram_macros.h instead.
HistogramBase* GetHistogramForTaskTraits(
    TaskTraits task_traits,
    HistogramBase* const (*histograms)[2]) {
  return histograms[static_cast<int>(task_traits.priority())]
                   [task_traits.may_block() ||
                            task_traits.with_base_sync_primitives()
                        ? 1
                        : 0];
}

// Returns shutdown behavior based on |traits|; returns SKIP_ON_SHUTDOWN if
// shutdown behavior is BLOCK_SHUTDOWN and |is_delayed|, because delayed tasks
// are not allowed to block shutdown.
TaskShutdownBehavior GetEffectiveShutdownBehavior(const TaskTraits& traits,
                                                  bool is_delayed) {
  const TaskShutdownBehavior shutdown_behavior = traits.shutdown_behavior();
  if (shutdown_behavior == TaskShutdownBehavior::BLOCK_SHUTDOWN && is_delayed) {
    return TaskShutdownBehavior::SKIP_ON_SHUTDOWN;
  }
  return shutdown_behavior;
}

}  // namespace

// Atomic internal state used by TaskTracker to track items that are blocking
// Shutdown. An "item" consist of either:
// - A running SKIP_ON_SHUTDOWN task
// - A queued/running BLOCK_SHUTDOWN TaskSource.
// Sequential consistency shouldn't be assumed from these calls (i.e. a thread
// reading |HasShutdownStarted() == true| isn't guaranteed to see all writes
// made before |StartShutdown()| on the thread that invoked it).
class TaskTracker::State {
 public:
  State() = default;

  // Sets a flag indicating that shutdown has started. Returns true if there are
  // items blocking shutdown. Can only be called once.
  bool StartShutdown() {
    const auto new_value =
        subtle::NoBarrier_AtomicIncrement(&bits_, kShutdownHasStartedMask);

    // Check that the "shutdown has started" bit isn't zero. This would happen
    // if it was incremented twice.
    DCHECK(new_value & kShutdownHasStartedMask);

    const auto num_items_blocking_shutdown =
        new_value >> kNumItemsBlockingShutdownBitOffset;
    return num_items_blocking_shutdown != 0;
  }

  // Returns true if shutdown has started.
  bool HasShutdownStarted() const {
    return subtle::NoBarrier_Load(&bits_) & kShutdownHasStartedMask;
  }

  // Returns true if there are items blocking shutdown.
  bool AreItemsBlockingShutdown() const {
    const auto num_items_blocking_shutdown =
        subtle::NoBarrier_Load(&bits_) >> kNumItemsBlockingShutdownBitOffset;
    DCHECK_GE(num_items_blocking_shutdown, 0);
    return num_items_blocking_shutdown != 0;
  }

  // Increments the number of items blocking shutdown. Returns true if
  // shutdown has started.
  bool IncrementNumItemsBlockingShutdown() {
#if DCHECK_IS_ON()
    // Verify that no overflow will occur.
    const auto num_items_blocking_shutdown =
        subtle::NoBarrier_Load(&bits_) >> kNumItemsBlockingShutdownBitOffset;
    DCHECK_LT(num_items_blocking_shutdown,
              std::numeric_limits<subtle::Atomic32>::max() -
                  kNumItemsBlockingShutdownIncrement);
#endif

    const auto new_bits = subtle::NoBarrier_AtomicIncrement(
        &bits_, kNumItemsBlockingShutdownIncrement);
    return new_bits & kShutdownHasStartedMask;
  }

  // Decrements the number of items blocking shutdown. Returns true if shutdown
  // has started and the number of tasks blocking shutdown becomes zero.
  bool DecrementNumItemsBlockingShutdown() {
    const auto new_bits = subtle::NoBarrier_AtomicIncrement(
        &bits_, -kNumItemsBlockingShutdownIncrement);
    const bool shutdown_has_started = new_bits & kShutdownHasStartedMask;
    const auto num_items_blocking_shutdown =
        new_bits >> kNumItemsBlockingShutdownBitOffset;
    DCHECK_GE(num_items_blocking_shutdown, 0);
    return shutdown_has_started && num_items_blocking_shutdown == 0;
  }

 private:
  static constexpr subtle::Atomic32 kShutdownHasStartedMask = 1;
  static constexpr subtle::Atomic32 kNumItemsBlockingShutdownBitOffset = 1;
  static constexpr subtle::Atomic32 kNumItemsBlockingShutdownIncrement =
      1 << kNumItemsBlockingShutdownBitOffset;

  // The LSB indicates whether shutdown has started. The other bits count the
  // number of items blocking shutdown.
  // No barriers are required to read/write |bits_| as this class is only used
  // as an atomic state checker, it doesn't provide sequential consistency
  // guarantees w.r.t. external state. Sequencing of the TaskTracker::State
  // operations themselves is guaranteed by the AtomicIncrement RMW (read-
  // modify-write) semantics however. For example, if two threads are racing to
  // call IncrementNumItemsBlockingShutdown() and StartShutdown() respectively,
  // either the first thread will win and the StartShutdown() call will see the
  // blocking task or the second thread will win and
  // IncrementNumItemsBlockingShutdown() will know that shutdown has started.
  subtle::Atomic32 bits_ = 0;

  DISALLOW_COPY_AND_ASSIGN(State);
};

// TODO(jessemckenna): Write a helper function to avoid code duplication below.
TaskTracker::TaskTracker(StringPiece histogram_label)
    : state_(new State),
      can_run_policy_(CanRunPolicy::kAll),
      flush_cv_(flush_lock_.CreateConditionVariable()),
      shutdown_lock_(&flush_lock_),
      task_latency_histograms_{
          {GetLatencyHistogram("TaskLatencyMicroseconds",
                               histogram_label,
                               "BackgroundTaskPriority"),
           GetLatencyHistogram("TaskLatencyMicroseconds",
                               histogram_label,
                               "BackgroundTaskPriority_MayBlock")},
          {GetLatencyHistogram("TaskLatencyMicroseconds",
                               histogram_label,
                               "UserVisibleTaskPriority"),
           GetLatencyHistogram("TaskLatencyMicroseconds",
                               histogram_label,
                               "UserVisibleTaskPriority_MayBlock")},
          {GetLatencyHistogram("TaskLatencyMicroseconds",
                               histogram_label,
                               "UserBlockingTaskPriority"),
           GetLatencyHistogram("TaskLatencyMicroseconds",
                               histogram_label,
                               "UserBlockingTaskPriority_MayBlock")}},
      heartbeat_latency_histograms_{
          {GetLatencyHistogram("HeartbeatLatencyMicroseconds",
                               histogram_label,
                               "BackgroundTaskPriority"),
           GetLatencyHistogram("HeartbeatLatencyMicroseconds",
                               histogram_label,
                               "BackgroundTaskPriority_MayBlock")},
          {GetLatencyHistogram("HeartbeatLatencyMicroseconds",
                               histogram_label,
                               "UserVisibleTaskPriority"),
           GetLatencyHistogram("HeartbeatLatencyMicroseconds",
                               histogram_label,
                               "UserVisibleTaskPriority_MayBlock")},
          {GetLatencyHistogram("HeartbeatLatencyMicroseconds",
                               histogram_label,
                               "UserBlockingTaskPriority"),
           GetLatencyHistogram("HeartbeatLatencyMicroseconds",
                               histogram_label,
                               "UserBlockingTaskPriority_MayBlock")}},
      num_tasks_run_while_queuing_histograms_{
          {GetCountHistogram("NumTasksRunWhileQueuing",
                             histogram_label,
                             "BackgroundTaskPriority"),
           GetCountHistogram("NumTasksRunWhileQueuing",
                             histogram_label,
                             "BackgroundTaskPriority_MayBlock")},
          {GetCountHistogram("NumTasksRunWhileQueuing",
                             histogram_label,
                             "UserVisibleTaskPriority"),
           GetCountHistogram("NumTasksRunWhileQueuing",
                             histogram_label,
                             "UserVisibleTaskPriority_MayBlock")},
          {GetCountHistogram("NumTasksRunWhileQueuing",
                             histogram_label,
                             "UserBlockingTaskPriority"),
           GetCountHistogram("NumTasksRunWhileQueuing",
                             histogram_label,
                             "UserBlockingTaskPriority_MayBlock")}},
      tracked_ref_factory_(this) {
  // Confirm that all |task_latency_histograms_| have been initialized above.
  DCHECK(*(&task_latency_histograms_[static_cast<int>(TaskPriority::HIGHEST) +
                                     1][0] -
           1));
}

TaskTracker::~TaskTracker() = default;

void TaskTracker::StartShutdown() {
  CheckedAutoLock auto_lock(shutdown_lock_);

  // This method can only be called once.
  DCHECK(!shutdown_event_);
  DCHECK(!state_->HasShutdownStarted());

  shutdown_event_ = std::make_unique<WaitableEvent>();

  const bool tasks_are_blocking_shutdown = state_->StartShutdown();

  // From now, if a thread causes the number of tasks blocking shutdown to
  // become zero, it will call OnBlockingShutdownTasksComplete().

  if (!tasks_are_blocking_shutdown) {
    // If another thread posts a BLOCK_SHUTDOWN task at this moment, it will
    // block until this method releases |shutdown_lock_|. Then, it will fail
    // DCHECK(!shutdown_event_->IsSignaled()). This is the desired behavior
    // because posting a BLOCK_SHUTDOWN task after StartShutdown() when no
    // tasks are blocking shutdown isn't allowed.
    shutdown_event_->Signal();
    return;
  }
}

// TODO(gab): Figure out why TS_UNCHECKED_READ is insufficient to make thread
// analysis of |shutdown_event_| happy on POSIX.
void TaskTracker::CompleteShutdown() NO_THREAD_SAFETY_ANALYSIS {
  // It is safe to access |shutdown_event_| without holding |lock_| because the
  // pointer never changes after being set by StartShutdown(), which must be
  // called before this.
  DCHECK(TS_UNCHECKED_READ(shutdown_event_));
  {
    base::ScopedAllowBaseSyncPrimitives allow_wait;
    TS_UNCHECKED_READ(shutdown_event_)->Wait();
  }

  // Unblock FlushForTesting() and perform the FlushAsyncForTesting callback
  // when shutdown completes.
  {
    CheckedAutoLock auto_lock(flush_lock_);
    flush_cv_->Signal();
  }
  CallFlushCallbackForTesting();
}

void TaskTracker::FlushForTesting() {
  CheckedAutoLock auto_lock(flush_lock_);
  while (num_incomplete_task_sources_.load(std::memory_order_acquire) != 0 &&
         !IsShutdownComplete()) {
    flush_cv_->Wait();
  }
}

void TaskTracker::FlushAsyncForTesting(OnceClosure flush_callback) {
  DCHECK(flush_callback);
  {
    CheckedAutoLock auto_lock(flush_lock_);
    DCHECK(!flush_callback_for_testing_)
        << "Only one FlushAsyncForTesting() may be pending at any time.";
    flush_callback_for_testing_ = std::move(flush_callback);
  }

  if (num_incomplete_task_sources_.load(std::memory_order_acquire) == 0 ||
      IsShutdownComplete()) {
    CallFlushCallbackForTesting();
  }
}

void TaskTracker::SetCanRunPolicy(CanRunPolicy can_run_policy) {
  can_run_policy_.store(can_run_policy);
}

bool TaskTracker::WillPostTask(Task* task,
                               TaskShutdownBehavior shutdown_behavior) {
  DCHECK(task);
  DCHECK(task->task);

  if (state_->HasShutdownStarted()) {
    // A non BLOCK_SHUTDOWN task is allowed to be posted iff shutdown hasn't
    // started.
    if (GetEffectiveShutdownBehavior(shutdown_behavior,
                                     !task->delayed_run_time.is_null()) !=
        TaskShutdownBehavior::BLOCK_SHUTDOWN) {
      return false;
    }

    // A BLOCK_SHUTDOWN task posted after shutdown has completed is an
    // ordering bug. This aims to catch those early.
    CheckedAutoLock auto_lock(shutdown_lock_);
    DCHECK(shutdown_event_);
    // TODO(http://crbug.com/698140): Atomically shutdown the service thread
    // to prevent racily posting BLOCK_SHUTDOWN tasks in response to a
    // FileDescriptorWatcher (and/or make such notifications never be
    // BLOCK_SHUTDOWN). Then, enable this DCHECK, until then, skip the task.
    // DCHECK(!shutdown_event_->IsSignaled());
    if (shutdown_event_->IsSignaled())
      return false;
  }

  // TODO(scheduler-dev): Record the task traits here.
  task_annotator_.WillQueueTask("ThreadPool_PostTask", task, "");

  return true;
}

RegisteredTaskSource TaskTracker::WillQueueTaskSource(
    scoped_refptr<TaskSource> task_source) {
  DCHECK(task_source);

  TaskShutdownBehavior shutdown_behavior = task_source->shutdown_behavior();
  if (!BeforeQueueTaskSource(shutdown_behavior))
    return nullptr;

  num_incomplete_task_sources_.fetch_add(1, std::memory_order_relaxed);
  return RegisteredTaskSource(std::move(task_source), this);
}

bool TaskTracker::CanRunPriority(TaskPriority priority) const {
  auto can_run_policy = can_run_policy_.load();

  if (can_run_policy == CanRunPolicy::kAll)
    return true;

  if (can_run_policy == CanRunPolicy::kForegroundOnly &&
      priority >= TaskPriority::USER_VISIBLE) {
    return true;
  }

  return false;
}

RegisteredTaskSource TaskTracker::RunAndPopNextTask(
    RegisteredTaskSource task_source) {
  DCHECK(task_source);

  // Run the next task in |task_source|.
  Optional<Task> task;
  TaskTraits traits;
  {
    TaskSource::Transaction task_source_transaction(
        task_source->BeginTransaction());
    task = task_source_transaction.TakeTask();
    traits = task_source_transaction.traits();
  }

  if (task) {
    const TaskShutdownBehavior effective_shutdown_behavior =
        GetEffectiveShutdownBehavior(task_source->shutdown_behavior(),
                                     !task->delayed_run_time.is_null());

    const bool can_run_task = BeforeRunTask(effective_shutdown_behavior);

    RunOrSkipTask(std::move(task.value()), task_source.get(), traits,
                  can_run_task);

    const bool task_source_must_be_queued =
        task_source->BeginTransaction().DidRunTask();

    if (can_run_task) {
      IncrementNumTasksRun();
      AfterRunTask(effective_shutdown_behavior);
    }

    // |task_source| should be reenqueued iff requested by DidRunTask().
    if (task_source_must_be_queued) {
      return task_source;
    }
  }

  return nullptr;
}

bool TaskTracker::HasShutdownStarted() const {
  return state_->HasShutdownStarted();
}

bool TaskTracker::IsShutdownComplete() const {
  CheckedAutoLock auto_lock(shutdown_lock_);
  return shutdown_event_ && shutdown_event_->IsSignaled();
}

void TaskTracker::RecordLatencyHistogram(
    LatencyHistogramType latency_histogram_type,
    TaskTraits task_traits,
    TimeTicks posted_time) const {
  const TimeDelta task_latency = TimeTicks::Now() - posted_time;

  DCHECK(latency_histogram_type == LatencyHistogramType::TASK_LATENCY ||
         latency_histogram_type == LatencyHistogramType::HEARTBEAT_LATENCY);
  const auto& histograms =
      latency_histogram_type == LatencyHistogramType::TASK_LATENCY
          ? task_latency_histograms_
          : heartbeat_latency_histograms_;
  GetHistogramForTaskTraits(task_traits, histograms)
      ->AddTimeMicrosecondsGranularity(task_latency);
}

void TaskTracker::RecordHeartbeatLatencyAndTasksRunWhileQueuingHistograms(
    TaskPriority task_priority,
    bool may_block,
    TimeTicks posted_time,
    int num_tasks_run_when_posted) const {
  TaskTraits task_traits;
  if (may_block)
    task_traits = TaskTraits(task_priority, MayBlock());
  else
    task_traits = TaskTraits(task_priority);
  RecordLatencyHistogram(LatencyHistogramType::HEARTBEAT_LATENCY, task_traits,
                         posted_time);
  GetHistogramForTaskTraits(task_traits,
                            num_tasks_run_while_queuing_histograms_)
      ->Add(GetNumTasksRun() - num_tasks_run_when_posted);
}

int TaskTracker::GetNumTasksRun() const {
  return num_tasks_run_.load(std::memory_order_relaxed);
}

void TaskTracker::IncrementNumTasksRun() {
  num_tasks_run_.fetch_add(1, std::memory_order_relaxed);
}

void TaskTracker::RunOrSkipTask(Task task,
                                TaskSource* task_source,
                                const TaskTraits& traits,
                                bool can_run_task) {
  DCHECK(task_source);
  RecordLatencyHistogram(LatencyHistogramType::TASK_LATENCY, traits,
                         task.queue_time);

  const auto environment = task_source->GetExecutionEnvironment();

  const bool previous_singleton_allowed =
      ThreadRestrictions::SetSingletonAllowed(
          traits.shutdown_behavior() !=
          TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN);
  const bool previous_io_allowed =
      ThreadRestrictions::SetIOAllowed(traits.may_block());
  const bool previous_wait_allowed =
      ThreadRestrictions::SetWaitAllowed(traits.with_base_sync_primitives());

  {
    DCHECK(environment.token.IsValid());
    ScopedSetSequenceTokenForCurrentThread
        scoped_set_sequence_token_for_current_thread(environment.token);
    ScopedSetTaskPriorityForCurrentThread
        scoped_set_task_priority_for_current_thread(traits.priority());

    // Local storage map used if none is provided by |environment|.
    Optional<SequenceLocalStorageMap> local_storage_map;
    if (!environment.sequence_local_storage)
      local_storage_map.emplace();

    ScopedSetSequenceLocalStorageMapForCurrentThread
        scoped_set_sequence_local_storage_map_for_current_thread(
            environment.sequence_local_storage
                ? environment.sequence_local_storage
                : &local_storage_map.value());

    // Set up TaskRunnerHandle as expected for the scope of the task.
    Optional<SequencedTaskRunnerHandle> sequenced_task_runner_handle;
    Optional<ThreadTaskRunnerHandle> single_thread_task_runner_handle;
    switch (task_source->execution_mode()) {
      case TaskSourceExecutionMode::kParallel:
        break;
      case TaskSourceExecutionMode::kSequenced:
        DCHECK(task_source->task_runner());
        sequenced_task_runner_handle.emplace(
            static_cast<SequencedTaskRunner*>(task_source->task_runner()));
        break;
      case TaskSourceExecutionMode::kSingleThread:
        DCHECK(task_source->task_runner());
        single_thread_task_runner_handle.emplace(
            static_cast<SingleThreadTaskRunner*>(task_source->task_runner()));
        break;
    }

    if (can_run_task) {
      TRACE_TASK_EXECUTION("ThreadPool_RunTask", task);

      // TODO(gab): In a better world this would be tacked on as an extra arg
      // to the trace event generated above. This is not possible however until
      // http://crbug.com/652692 is resolved.
      TRACE_EVENT1("thread_pool", "ThreadPool_TaskInfo", "task_info",
                   std::make_unique<TaskTracingInfo>(
                       traits,
                       kExecutionModeString[static_cast<size_t>(
                           task_source->execution_mode())],
                       environment.token));

      RunTaskWithShutdownBehavior(traits.shutdown_behavior(), &task);
    }

    // Make sure the arguments bound to the callback are deleted within the
    // scope in which the callback runs.
    task.task = OnceClosure();
  }

  ThreadRestrictions::SetWaitAllowed(previous_wait_allowed);
  ThreadRestrictions::SetIOAllowed(previous_io_allowed);
  ThreadRestrictions::SetSingletonAllowed(previous_singleton_allowed);
}

bool TaskTracker::HasIncompleteTaskSourcesForTesting() const {
  return num_incomplete_task_sources_.load(std::memory_order_acquire) != 0;
}

bool TaskTracker::BeforeQueueTaskSource(
    TaskShutdownBehavior shutdown_behavior) {
  if (shutdown_behavior == TaskShutdownBehavior::BLOCK_SHUTDOWN) {
    // BLOCK_SHUTDOWN task sources block shutdown between the moment they are
    // queued and the moment their last task completes its execution.
    const bool shutdown_started = state_->IncrementNumItemsBlockingShutdown();

    if (shutdown_started) {

      // A BLOCK_SHUTDOWN task posted after shutdown has completed is an
      // ordering bug. This aims to catch those early.
      CheckedAutoLock auto_lock(shutdown_lock_);
      DCHECK(shutdown_event_);
      // TODO(http://crbug.com/698140): Atomically shutdown the service thread
      // to prevent racily posting BLOCK_SHUTDOWN tasks in response to a
      // FileDescriptorWatcher (and/or make such notifications never be
      // BLOCK_SHUTDOWN). Then, enable this DCHECK, until then, skip the task.
      // DCHECK(!shutdown_event_->IsSignaled());
      if (shutdown_event_->IsSignaled()) {
        state_->DecrementNumItemsBlockingShutdown();
        return false;
      }
    }

    return true;
  }

  // A non BLOCK_SHUTDOWN task is allowed to be posted iff shutdown hasn't
  // started.
  return !state_->HasShutdownStarted();
}

bool TaskTracker::BeforeRunTask(
    TaskShutdownBehavior effective_shutdown_behavior) {
  switch (effective_shutdown_behavior) {
    case TaskShutdownBehavior::BLOCK_SHUTDOWN: {
      // The number of tasks blocking shutdown has been incremented when the
      // task was posted.
      DCHECK(state_->AreItemsBlockingShutdown());

      // Trying to run a BLOCK_SHUTDOWN task after shutdown has completed is
      // unexpected as it either shouldn't have been posted if shutdown
      // completed or should be blocking shutdown if it was posted before it
      // did.
      DCHECK(!state_->HasShutdownStarted() || !IsShutdownComplete());

      return true;
    }

    case TaskShutdownBehavior::SKIP_ON_SHUTDOWN: {
      // SKIP_ON_SHUTDOWN tasks block shutdown while they are running.
      const bool shutdown_started = state_->IncrementNumItemsBlockingShutdown();

      if (shutdown_started) {
        // The SKIP_ON_SHUTDOWN task isn't allowed to run during shutdown.
        // Decrement the number of tasks blocking shutdown that was wrongly
        // incremented.
        DecrementNumItemsBlockingShutdown();
        return false;
      }

      return true;
    }

    case TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN: {
      return !state_->HasShutdownStarted();
    }
  }

  NOTREACHED();
  return false;
}

void TaskTracker::AfterRunTask(
    TaskShutdownBehavior effective_shutdown_behavior) {
  if (effective_shutdown_behavior == TaskShutdownBehavior::SKIP_ON_SHUTDOWN) {
    DecrementNumItemsBlockingShutdown();
  }
}

scoped_refptr<TaskSource> TaskTracker::UnregisterTaskSource(
    scoped_refptr<TaskSource> task_source) {
  DCHECK(task_source);
  if (task_source->shutdown_behavior() ==
      TaskShutdownBehavior::BLOCK_SHUTDOWN) {
    DecrementNumItemsBlockingShutdown();
  }
  DecrementNumIncompleteTaskSources();
  return task_source;
}

void TaskTracker::DecrementNumItemsBlockingShutdown() {
  const bool shutdown_started_and_no_items_block_shutdown =
      state_->DecrementNumItemsBlockingShutdown();
  if (!shutdown_started_and_no_items_block_shutdown)
    return;

  CheckedAutoLock auto_lock(shutdown_lock_);
  DCHECK(shutdown_event_);
  shutdown_event_->Signal();
}

void TaskTracker::DecrementNumIncompleteTaskSources() {
  const auto prev_num_incomplete_task_sources =
      num_incomplete_task_sources_.fetch_sub(1);
  DCHECK_GE(prev_num_incomplete_task_sources, 1);
  if (prev_num_incomplete_task_sources == 1) {
    {
      CheckedAutoLock auto_lock(flush_lock_);
      flush_cv_->Signal();
    }
    CallFlushCallbackForTesting();
  }
}

void TaskTracker::CallFlushCallbackForTesting() {
  OnceClosure flush_callback;
  {
    CheckedAutoLock auto_lock(flush_lock_);
    flush_callback = std::move(flush_callback_for_testing_);
  }
  if (flush_callback)
    std::move(flush_callback).Run();
}

NOINLINE void TaskTracker::RunContinueOnShutdown(Task* task) {
  const int line_number = __LINE__;
  task_annotator_.RunTask("ThreadPool_RunTask_ContinueOnShutdown", task);
  base::debug::Alias(&line_number);
}

NOINLINE void TaskTracker::RunSkipOnShutdown(Task* task) {
  const int line_number = __LINE__;
  task_annotator_.RunTask("ThreadPool_RunTask_SkipOnShutdown", task);
  base::debug::Alias(&line_number);
}

NOINLINE void TaskTracker::RunBlockShutdown(Task* task) {
  const int line_number = __LINE__;
  task_annotator_.RunTask("ThreadPool_RunTask_BlockShutdown", task);
  base::debug::Alias(&line_number);
}

void TaskTracker::RunTaskWithShutdownBehavior(
    TaskShutdownBehavior shutdown_behavior,
    Task* task) {
  switch (shutdown_behavior) {
    case TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN:
      RunContinueOnShutdown(task);
      return;
    case TaskShutdownBehavior::SKIP_ON_SHUTDOWN:
      RunSkipOnShutdown(task);
      return;
    case TaskShutdownBehavior::BLOCK_SHUTDOWN:
      RunBlockShutdown(task);
      return;
  }
}

}  // namespace internal
}  // namespace base
