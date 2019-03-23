// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SEQUENCE_MANAGER_SEQUENCE_MANAGER_H_
#define BASE_TASK_SEQUENCE_MANAGER_SEQUENCE_MANAGER_H_

#include <memory>
#include <utility>

#include "base/message_loop/message_loop.h"
#include "base/message_loop/timer_slack.h"
#include "base/single_thread_task_runner.h"
#include "base/task/sequence_manager/task_queue_impl.h"
#include "base/task/sequence_manager/task_time_observer.h"
#include "base/time/default_tick_clock.h"

namespace base {
namespace sequence_manager {

class TimeDomain;

// SequenceManager manages TaskQueues which have different properties
// (e.g. priority, common task type) multiplexing all posted tasks into
// a single backing sequence (currently bound to a single thread, which is
// refererred as *main thread* in the comments below). SequenceManager
// implementation can be used in a various ways to apply scheduling logic.
class SequenceManager {
 public:
  class Observer {
   public:
    virtual ~Observer() = default;
    // Called back on the main thread.
    virtual void OnBeginNestedRunLoop() = 0;
    virtual void OnExitNestedRunLoop() = 0;
  };

  struct MetricRecordingSettings {
    // This parameter will be updated for consistency on creation (setting
    // value to 0 when ThreadTicks are not supported).
    MetricRecordingSettings(double task_sampling_rate_for_recording_cpu_time);

    // The proportion of the tasks for which the cpu time will be
    // sampled or 0 if this is not enabled.
    // Since randomised sampling requires the use of Rand(), it is enabled only
    // on platforms which support it.
    // If it is 1 then cpu time is measured for each task, so the integral
    // metrics (as opposed to per-task metrics) can be recorded.
    double task_sampling_rate_for_recording_cpu_time = 0;

    bool records_cpu_time_for_some_tasks() const {
      return task_sampling_rate_for_recording_cpu_time > 0.0;
    }

    bool records_cpu_time_for_all_tasks() const {
      return task_sampling_rate_for_recording_cpu_time == 1.0;
    }
  };

  // Settings defining the desired SequenceManager behaviour: the type of the
  // MessageLoop and whether randomised sampling should be enabled.
  struct Settings {
    Settings() = default;
    // In the future MessagePump (which is move-only) will also be a setting,
    // so we are making Settings move-only in preparation.
    Settings(Settings&& move_from) noexcept = default;

    MessageLoop::Type message_loop_type = MessageLoop::Type::TYPE_DEFAULT;
    bool randomised_sampling_enabled = false;
    const TickClock* clock = DefaultTickClock::GetInstance();

    DISALLOW_COPY_AND_ASSIGN(Settings);
  };

  virtual ~SequenceManager() = default;

  // Binds the SequenceManager and its TaskQueues to the current thread. Should
  // only be called once. Note that CreateSequenceManagerOnCurrentThread()
  // performs this initialization automatically.
  virtual void BindToCurrentThread() = 0;

  // Finishes the initialization for a SequenceManager created via
  // CreateUnboundSequenceManager(nullptr). Must not be called in any other
  // circumstances. Note it's assumed |message_loop| outlives the
  // SequenceManager.
  virtual void BindToMessageLoop(MessageLoopBase* message_loop_base) = 0;

  // Finishes the initialization for a SequenceManager created via
  // CreateUnboundSequenceManagerWithPump(). Must not be called in any other
  // circumstances. The ownership of the pump is transferred to SequenceManager.
  virtual void BindToMessagePump(std::unique_ptr<MessagePump> message_pump) = 0;

  // Initializes the SequenceManager on the bound thread. Should only be called
  // once and only after the ThreadController's dependencies were initialized.
  // Note that CreateSequenceManagerOnCurrentThread() performs this
  // initialization automatically.
  //
  // TODO(eseckler): This currently needs to be separate from
  // BindToCurrentThread() as it requires that the MessageLoop is bound
  // (otherwise we can't add a NestingObserver), while BindToCurrentThread()
  // requires that the MessageLoop has not yet been bound (binding the
  // MessageLoop would fail if its TaskRunner, i.e. the default task queue, had
  // not yet been bound). Reconsider this API once we get rid of MessageLoop.
  virtual void CompleteInitializationOnBoundThread() = 0;

  // TODO(kraynov): Bring back CreateOnCurrentThread static method here
  // when the move is done. It's not here yet to reduce PLATFORM_EXPORT
  // macros hacking during the move.

  // Must be called on the main thread.
  // Can be called only once, before creating TaskQueues.
  // Observer must outlive the SequenceManager.
  virtual void SetObserver(Observer* observer) = 0;

  // Must be called on the main thread.
  virtual void AddTaskTimeObserver(TaskTimeObserver* task_time_observer) = 0;
  virtual void RemoveTaskTimeObserver(TaskTimeObserver* task_time_observer) = 0;

  // Registers a TimeDomain with SequenceManager.
  // TaskQueues must only be created with a registered TimeDomain.
  // Conversely, any TimeDomain must remain registered until no
  // TaskQueues (using that TimeDomain) remain.
  virtual void RegisterTimeDomain(TimeDomain* time_domain) = 0;
  virtual void UnregisterTimeDomain(TimeDomain* time_domain) = 0;

  virtual TimeDomain* GetRealTimeDomain() const = 0;
  virtual const TickClock* GetTickClock() const = 0;
  virtual TimeTicks NowTicks() const = 0;

  // Sets the SingleThreadTaskRunner that will be returned by
  // ThreadTaskRunnerHandle::Get on the main thread.
  virtual void SetDefaultTaskRunner(
      scoped_refptr<SingleThreadTaskRunner> task_runner) = 0;

  // Removes all canceled delayed tasks, and considers resizing to fit all
  // internal queues.
  virtual void ReclaimMemory() = 0;

  // Returns true if no tasks were executed in TaskQueues that monitor
  // quiescence since the last call to this method.
  virtual bool GetAndClearSystemIsQuiescentBit() = 0;

  // Set the number of tasks executed in a single SequenceManager invocation.
  // Increasing this number reduces the overhead of the tasks dispatching
  // logic at the cost of a potentially worse latency. 1 by default.
  virtual void SetWorkBatchSize(int work_batch_size) = 0;

  // Requests desired timer precision from the OS.
  // Has no effect on some platforms.
  virtual void SetTimerSlack(TimerSlack timer_slack) = 0;

  // Enables crash keys that can be set in the scope of a task which help
  // to identify the culprit if upcoming work results in a crash.
  // Key names must be thread-specific to avoid races and corrupted crash dumps.
  virtual void EnableCrashKeys(const char* file_name_crash_key,
                               const char* function_name_crash_key) = 0;

  // Returns the metric recording configuration for the current SequenceManager.
  virtual const MetricRecordingSettings& GetMetricRecordingSettings() const = 0;

  // Creates a task queue with the given type, |spec| and args.
  // Must be called on the main thread.
  // TODO(scheduler-dev): SequenceManager should not create TaskQueues.
  template <typename TaskQueueType, typename... Args>
  scoped_refptr<TaskQueueType> CreateTaskQueueWithType(
      const TaskQueue::Spec& spec,
      Args&&... args) {
    return WrapRefCounted(new TaskQueueType(CreateTaskQueueImpl(spec), spec,
                                            std::forward<Args>(args)...));
  }

  // Creates a vanilla TaskQueue rather than a user type derived from it. This
  // should be used if you don't wish to sub class TaskQueue.
  // Must be called on the main thread.
  virtual scoped_refptr<TaskQueue> CreateTaskQueue(
      const TaskQueue::Spec& spec) = 0;

  // Returns true iff this SequenceManager has no immediate work to do. I.e.
  // there are no pending non-delayed tasks or delayed tasks that are due to
  // run. This method ignores any pending delayed tasks that might have become
  // eligible to run since the last task was executed. This is important because
  // if it did tests would become flaky depending on the exact timing of this
  // call.
  virtual bool IsIdleForTesting() = 0;

  // The total number of posted tasks that haven't executed yet.
  virtual size_t GetPendingTaskCountForTesting() const = 0;

  // Returns a JSON string which describes all pending tasks.
  virtual std::string DescribeAllPendingTasks() const = 0;

 protected:
  virtual std::unique_ptr<internal::TaskQueueImpl> CreateTaskQueueImpl(
      const TaskQueue::Spec& spec) = 0;
};

// Create SequenceManager using MessageLoop on the current thread.
// Implementation is located in sequence_manager_impl.cc.
// TODO(scheduler-dev): Remove after every thread has a SequenceManager.
BASE_EXPORT std::unique_ptr<SequenceManager>
CreateSequenceManagerOnCurrentThread(SequenceManager::Settings settings);

// Create a SequenceManager using the given MessagePump on the current thread.
// MessagePump instances can be created with
// MessageLoop::CreateMessagePumpForType().
BASE_EXPORT std::unique_ptr<SequenceManager>
CreateSequenceManagerOnCurrentThreadWithPump(
    std::unique_ptr<MessagePump> message_pump,
    SequenceManager::Settings settings = SequenceManager::Settings());

// Create a SequenceManager for a future thread using the provided MessageLoop.
// The SequenceManager can be initialized on the current thread and then needs
// to be bound and initialized on the target thread by calling
// BindToCurrentThread() and CompleteInitializationOnBoundThread() during the
// thread's startup.
//
// Implementation is located in sequence_manager_impl.cc. TODO(scheduler-dev):
// Remove when we get rid of MessageLoop.
// TODO(scheduler-dev): Change this to CreateUnboundSequenceManagerWithPump.
BASE_EXPORT std::unique_ptr<SequenceManager> CreateUnboundSequenceManager(
    MessageLoopBase* message_loop_base,
    SequenceManager::Settings settings = SequenceManager::Settings());

}  // namespace sequence_manager
}  // namespace base

#endif  // BASE_TASK_SEQUENCE_MANAGER_SEQUENCE_MANAGER_H_
