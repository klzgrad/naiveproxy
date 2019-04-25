// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SEQUENCE_MANAGER_SEQUENCE_MANAGER_IMPL_H_
#define BASE_TASK_SEQUENCE_MANAGER_SEQUENCE_MANAGER_IMPL_H_

#include <list>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/atomic_sequence_num.h"
#include "base/cancelable_callback.h"
#include "base/containers/circular_deque.h"
#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/pending_task.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "base/task/common/task_annotator.h"
#include "base/task/sequence_manager/associated_thread_id.h"
#include "base/task/sequence_manager/enqueue_order.h"
#include "base/task/sequence_manager/moveable_auto_lock.h"
#include "base/task/sequence_manager/sequence_manager.h"
#include "base/task/sequence_manager/task_queue_impl.h"
#include "base/task/sequence_manager/task_queue_selector.h"
#include "base/task/sequence_manager/thread_controller.h"
#include "base/threading/thread_checker.h"
#include "base/time/default_tick_clock.h"
#include "build/build_config.h"

namespace base {

namespace debug {
struct CrashKeyString;
}  // namespace debug

namespace trace_event {
class ConvertableToTraceFormat;
}  // namespace trace_event

namespace sequence_manager {

class SequenceManagerForTest;
class TaskQueue;
class TaskTimeObserver;
class TimeDomain;

namespace internal {

class RealTimeDomain;
class TaskQueueImpl;

// The task queue manager provides N task queues and a selector interface for
// choosing which task queue to service next. Each task queue consists of two
// sub queues:
//
// 1. Incoming task queue. Tasks that are posted get immediately appended here.
//    When a task is appended into an empty incoming queue, the task manager
//    work function (DoWork()) is scheduled to run on the main task runner.
//
// 2. Work queue. If a work queue is empty when DoWork() is entered, tasks from
//    the incoming task queue (if any) are moved here. The work queues are
//    registered with the selector as input to the scheduling decision.
//
class BASE_EXPORT SequenceManagerImpl
    : public SequenceManager,
      public internal::SequencedTaskSource,
      public internal::TaskQueueSelector::Observer,
      public RunLoop::NestingObserver,
      public MessageLoopBase {
 public:
  using Observer = SequenceManager::Observer;

  ~SequenceManagerImpl() override;

  // Assume direct control over current thread and create a SequenceManager.
  // This function should be called only once per thread.
  // This function assumes that a MessageLoop is initialized for
  // the current thread.
  static std::unique_ptr<SequenceManagerImpl> CreateOnCurrentThread(
      SequenceManager::Settings settings = SequenceManager::Settings());

  // Create an unbound SequenceManager (typically for a future thread). The
  // SequenceManager can be initialized on the current thread and then needs to
  // be bound and initialized on the target thread by calling one of the Bind*()
  // methods.
  static std::unique_ptr<SequenceManagerImpl> CreateUnbound(
      SequenceManager::Settings settings);

  // SequenceManager implementation:
  void BindToCurrentThread() override;
  void BindToMessageLoop(MessageLoopBase* message_loop_base) override;
  void BindToMessagePump(std::unique_ptr<MessagePump> message_pump) override;
  void SetObserver(Observer* observer) override;
  void AddTaskTimeObserver(TaskTimeObserver* task_time_observer) override;
  void RemoveTaskTimeObserver(TaskTimeObserver* task_time_observer) override;
  void RegisterTimeDomain(TimeDomain* time_domain) override;
  void UnregisterTimeDomain(TimeDomain* time_domain) override;
  TimeDomain* GetRealTimeDomain() const override;
  const TickClock* GetTickClock() const override;
  TimeTicks NowTicks() const override;
  void SetDefaultTaskRunner(
      scoped_refptr<SingleThreadTaskRunner> task_runner) override;
  void ReclaimMemory() override;
  bool GetAndClearSystemIsQuiescentBit() override;
  void SetWorkBatchSize(int work_batch_size) override;
  void SetTimerSlack(TimerSlack timer_slack) override;
  void EnableCrashKeys(const char* file_name_crash_key,
                       const char* function_name_crash_key) override;
  const MetricRecordingSettings& GetMetricRecordingSettings() const override;
  size_t GetPendingTaskCountForTesting() const override;
  scoped_refptr<TaskQueue> CreateTaskQueue(
      const TaskQueue::Spec& spec) override;
  std::string DescribeAllPendingTasks() const override;

  // SequencedTaskSource implementation:
  Optional<PendingTask> TakeTask() override;
  void DidRunTask() override;
  TimeDelta DelayTillNextTask(LazyNow* lazy_now) const override;
  bool HasPendingHighResolutionTasks() override;
  bool OnSystemIdle() override;

  // MessageLoopBase implementation:
  void AddTaskObserver(MessageLoop::TaskObserver* task_observer) override;
  void RemoveTaskObserver(MessageLoop::TaskObserver* task_observer) override;
  void AddDestructionObserver(
      MessageLoopCurrent::DestructionObserver* destruction_observer) override;
  void RemoveDestructionObserver(
      MessageLoopCurrent::DestructionObserver* destruction_observer) override;
  // TODO(alexclarke): Remove this as part of https://crbug.com/825327.
  void SetTaskRunner(
      scoped_refptr<SingleThreadTaskRunner> task_runner) override;
  // TODO(alexclarke): Remove this as part of https://crbug.com/825327.
  scoped_refptr<SingleThreadTaskRunner> GetTaskRunner() override;
  std::string GetThreadName() const override;
  bool IsBoundToCurrentThread() const override;
  MessagePump* GetMessagePump() const override;
  bool IsType(MessageLoop::Type type) const override;
  void SetAddQueueTimeToTasks(bool enable) override;
  void SetTaskExecutionAllowed(bool allowed) override;
  bool IsTaskExecutionAllowed() const override;
#if defined(OS_IOS)
  void AttachToMessagePump() override;
#endif
  bool IsIdleForTesting() override;
  void BindToCurrentThread(std::unique_ptr<MessagePump> pump) override;
  void DeletePendingTasks() override;
  bool HasTasks() override;
  MessageLoop::Type GetType() const override;

  // Requests that a task to process work is scheduled.
  void ScheduleWork();

  // Requests that a delayed task to process work is posted on the main task
  // runner. These delayed tasks are de-duplicated. Must be called on the thread
  // this class was created on.

  // Schedules next wake-up at the given time, cancels any previous requests.
  // Use TimeTicks::Max() to cancel a wake-up.
  // Must be called from a TimeDomain only.
  void SetNextDelayedDoWork(LazyNow* lazy_now, TimeTicks run_time);

  // Returns the currently executing TaskQueue if any. Must be called on the
  // thread this class was created on.
  internal::TaskQueueImpl* currently_executing_task_queue() const;

  // Unregisters a TaskQueue previously created by |NewTaskQueue()|.
  // No tasks will run on this queue after this call.
  void UnregisterTaskQueueImpl(
      std::unique_ptr<internal::TaskQueueImpl> task_queue);

  // Schedule a call to UnregisterTaskQueueImpl as soon as it's safe to do so.
  void ShutdownTaskQueueGracefully(
      std::unique_ptr<internal::TaskQueueImpl> task_queue);

  const scoped_refptr<AssociatedThreadId>& associated_thread() const {
    return associated_thread_;
  }

  WeakPtr<SequenceManagerImpl> GetWeakPtr();

  // How frequently to perform housekeeping tasks (sweeping canceled tasks etc).
  static constexpr TimeDelta kReclaimMemoryInterval =
      TimeDelta::FromSeconds(30);

 protected:
  // Create a task queue manager where |controller| controls the thread
  // on which the tasks are eventually run.
  SequenceManagerImpl(std::unique_ptr<internal::ThreadController> controller,
                      SequenceManager::Settings settings = Settings());

  friend class internal::TaskQueueImpl;
  friend class ::base::sequence_manager::SequenceManagerForTest;

 private:
  enum class ProcessTaskResult {
    kDeferred,
    kExecuted,
    kSequenceManagerDeleted,
  };

  // SequenceManager maintains a queue of non-nestable tasks since they're
  // uncommon and allocating an extra deque per TaskQueue will waste the memory.
  using NonNestableTaskDeque =
      circular_deque<internal::TaskQueueImpl::DeferredNonNestableTask>;

  // We have to track rentrancy because we support nested runloops but the
  // selector interface is unaware of those.  This struct keeps track off all
  // task related state needed to make pairs of TakeTask() / DidRunTask() work.
  struct ExecutingTask {
    ExecutingTask(Task&& task,
                  internal::TaskQueueImpl* task_queue,
                  TaskQueue::TaskTiming task_timing)
        : pending_task(std::move(task)),
          task_queue(task_queue),
          task_timing(task_timing),
          task_type(pending_task.task_type) {}

    Task pending_task;
    internal::TaskQueueImpl* task_queue = nullptr;
    TaskQueue::TaskTiming task_timing;
    // Save task metadata to use in after running a task as |pending_task|
    // won't be available then.
    int task_type;
  };

  struct MainThreadOnly {
    explicit MainThreadOnly(
        const scoped_refptr<AssociatedThreadId>& associated_thread,
        bool randomised_sampling_enabled);
    ~MainThreadOnly();

    int nesting_depth = 0;
    NonNestableTaskDeque non_nestable_task_queue;
    // TODO(altimin): Switch to instruction pointer crash key when it's
    // available.
    debug::CrashKeyString* file_name_crash_key = nullptr;
    debug::CrashKeyString* function_name_crash_key = nullptr;

    std::mt19937_64 random_generator;
    std::uniform_real_distribution<double> uniform_distribution;

    internal::TaskQueueSelector selector;
    ObserverList<MessageLoop::TaskObserver>::Unchecked task_observers;
    ObserverList<TaskTimeObserver>::Unchecked task_time_observers;
    std::set<TimeDomain*> time_domains;
    std::unique_ptr<internal::RealTimeDomain> real_time_domain;

    // If true MaybeReclaimMemory will attempt to reclaim memory.
    bool memory_reclaim_scheduled = false;

    // Used to ensure we don't perform expensive housekeeping too frequently.
    TimeTicks next_time_to_reclaim_memory;

    // List of task queues managed by this SequenceManager.
    // - active_queues contains queues that are still running tasks.
    //   Most often they are owned by relevant TaskQueues, but
    //   queues_to_gracefully_shutdown_ are included here too.
    // - queues_to_gracefully_shutdown contains queues which should be deleted
    //   when they become empty.
    // - queues_to_delete contains soon-to-be-deleted queues, because some
    //   internal scheduling code does not expect queues to be pulled
    //   from underneath.

    std::set<internal::TaskQueueImpl*> active_queues;

    std::map<internal::TaskQueueImpl*, std::unique_ptr<internal::TaskQueueImpl>>
        queues_to_gracefully_shutdown;
    std::map<internal::TaskQueueImpl*, std::unique_ptr<internal::TaskQueueImpl>>
        queues_to_delete;

    bool task_was_run_on_quiescence_monitored_queue = false;
    bool nesting_observer_registered_ = false;

    // Due to nested runloops more than one task can be executing concurrently.
    std::vector<ExecutingTask> task_execution_stack;

    Observer* observer = nullptr;  // NOT OWNED

    ObserverList<MessageLoopCurrent::DestructionObserver>::Unchecked
        destruction_observers;
  };

  void CompleteInitializationOnBoundThread();

  // TaskQueueSelector::Observer:
  void OnTaskQueueEnabled(internal::TaskQueueImpl* queue) override;

  // RunLoop::NestingObserver:
  void OnBeginNestedRunLoop() override;
  void OnExitNestedRunLoop() override;

  // Called by the task queue to inform this SequenceManager of a task that's
  // about to be queued. This SequenceManager may use this opportunity to add
  // metadata to |pending_task| before it is moved into the queue.
  void WillQueueTask(Task* pending_task);

  // Delayed Tasks with run_times <= Now() are enqueued onto the work queue and
  // reloads any empty work queues.
  void WakeUpReadyDelayedQueues(LazyNow* lazy_now);

  void NotifyWillProcessTask(ExecutingTask* task, LazyNow* time_before_task);
  void NotifyDidProcessTask(ExecutingTask* task, LazyNow* time_after_task);

  internal::EnqueueOrder GetNextSequenceNumber();

  bool GetAddQueueTimeToTasks();

  std::unique_ptr<trace_event::ConvertableToTraceFormat>
  AsValueWithSelectorResult(internal::WorkQueue* selected_work_queue,
                            bool force_verbose) const;

  AtomicFlagSet::AtomicFlag GetFlagToRequestReloadForEmptyQueue(
      TaskQueueImpl* task_queue);

  // Calls |TakeImmediateIncomingQueueTasks| on all queues with their reload
  // flag set in |empty_queues_to_reload_|.
  void ReloadEmptyWorkQueues() const;

  std::unique_ptr<internal::TaskQueueImpl> CreateTaskQueueImpl(
      const TaskQueue::Spec& spec) override;

  // Periodically reclaims memory by sweeping away canceled tasks and shrinking
  // buffers.
  void MaybeReclaimMemory();

  // Deletes queues marked for deletion and empty queues marked for shutdown.
  void CleanUpQueues();

  void RemoveAllCanceledTasksFromFrontOfWorkQueues();

  bool ShouldRecordTaskTiming(const internal::TaskQueueImpl* task_queue);
  bool ShouldRecordCPUTimeForTask();

  // Helper to terminate all scoped trace events to allow starting new ones
  // in TakeTask().
  Optional<PendingTask> TakeTaskImpl();

  // Determines if wall time or thread time should be recorded for the next
  // task.
  TaskQueue::TaskTiming InitializeTaskTiming(
      internal::TaskQueueImpl* task_queue);

  scoped_refptr<AssociatedThreadId> associated_thread_;

  internal::EnqueueOrder::Generator enqueue_order_generator_;

  const std::unique_ptr<internal::ThreadController> controller_;
  const MessageLoop::Type type_;

  const MetricRecordingSettings metric_recording_settings_;

  // Whether to add the queue time to tasks.
  base::subtle::Atomic32 add_queue_time_to_tasks_ = 0;

  AtomicFlagSet empty_queues_to_reload_;

  // A check to bail out early during memory corruption.
  // https://crbug.com/757940
  bool Validate();

  volatile int32_t memory_corruption_sentinel_;

  MainThreadOnly main_thread_only_;
  MainThreadOnly& main_thread_only() {
    DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
    return main_thread_only_;
  }
  const MainThreadOnly& main_thread_only() const {
    DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
    return main_thread_only_;
  }

  WeakPtrFactory<SequenceManagerImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(SequenceManagerImpl);
};

}  // namespace internal
}  // namespace sequence_manager
}  // namespace base

#endif  // BASE_TASK_SEQUENCE_MANAGER_SEQUENCE_MANAGER_IMPL_H_
