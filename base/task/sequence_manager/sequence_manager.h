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
    MetricRecordingSettings();
    // Note: These parameters are desired and MetricRecordingSetting's will
    // update them for consistency (e.g. setting values to false when
    // ThreadTicks are not supported).
    MetricRecordingSettings(bool records_cpu_time_for_each_task,
                            double task_sampling_rate_for_recording_cpu_time);

    // True if cpu time is measured for each task, so the integral
    // metrics (as opposed to per-task metrics) can be recorded.
    bool records_cpu_time_for_each_task = false;
    // The proportion of the tasks for which the cpu time will be
    // sampled or 0 if this is not enabled.
    // This value is always 1 if the |records_cpu_time_for_each_task| is true.
    double task_sampling_rate_for_recording_cpu_time = 0;
  };

  virtual ~SequenceManager() = default;

  // Binds the SequenceManager and its TaskQueues to the current thread. Should
  // only be called once. Note that CreateSequenceManagerOnCurrentThread()
  // performs this initialization automatically.
  virtual void BindToCurrentThread() = 0;

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
  virtual void AddTaskObserver(MessageLoop::TaskObserver* task_observer) = 0;
  virtual void RemoveTaskObserver(MessageLoop::TaskObserver* task_observer) = 0;
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

  // Removes all canceled delayed tasks.
  virtual void SweepCanceledDelayedTasks() = 0;

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
  scoped_refptr<TaskQueueType> CreateTaskQueue(const TaskQueue::Spec& spec,
                                               Args&&... args) {
    return WrapRefCounted(new TaskQueueType(CreateTaskQueueImpl(spec), spec,
                                            std::forward<Args>(args)...));
  }

 protected:
  virtual std::unique_ptr<internal::TaskQueueImpl> CreateTaskQueueImpl(
      const TaskQueue::Spec& spec) = 0;
};

// Create SequenceManager using MessageLoop on the current thread.
// Implementation is located in sequence_manager_impl.cc.
// TODO(scheduler-dev): Rename to TakeOverCurrentThread when we'll stop using
// MessageLoop and will actually take over a thread.
BASE_EXPORT std::unique_ptr<SequenceManager>
CreateSequenceManagerOnCurrentThread();

// Create a SequenceManager for a future thread using the provided MessageLoop.
// The SequenceManager can be initialized on the current thread and then needs
// to be bound and initialized on the target thread by calling
// BindToCurrentThread() and CompleteInitializationOnBoundThread() during the
// thread's startup.
//
// Implementation is located in sequence_manager_impl.cc. TODO(scheduler-dev):
// Remove when we get rid of MessageLoop.
BASE_EXPORT std::unique_ptr<SequenceManager> CreateUnboundSequenceManager(
    MessageLoop* message_loop);

}  // namespace sequence_manager
}  // namespace base

#endif  // BASE_TASK_SEQUENCE_MANAGER_SEQUENCE_MANAGER_H_
