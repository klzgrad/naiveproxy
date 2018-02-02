// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/stack_sampling_profiler.h"

#include <algorithm>
#include <map>
#include <utility>

#include "base/atomic_sequence_num.h"
#include "base/atomicops.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/lazy_instance.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/singleton.h"
#include "base/profiler/native_stack_sampler.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/timer/elapsed_timer.h"

namespace base {

namespace {

// This value is used when there is no collection in progress and thus no ID
// for referencing the active collection to the SamplingThread.
const int NULL_PROFILER_ID = -1;

void ChangeAtomicFlags(subtle::Atomic32* flags,
                       subtle::Atomic32 set,
                       subtle::Atomic32 clear) {
  DCHECK(set != 0 || clear != 0);
  DCHECK_EQ(0, set & clear);

  subtle::Atomic32 bits = subtle::NoBarrier_Load(flags);
  while (true) {
    subtle::Atomic32 existing =
        subtle::NoBarrier_CompareAndSwap(flags, bits, (bits | set) & ~clear);
    if (existing == bits)
      break;
    bits = existing;
  }
}

}  // namespace

// StackSamplingProfiler::Module ----------------------------------------------

StackSamplingProfiler::Module::Module() : base_address(0u) {}
StackSamplingProfiler::Module::Module(uintptr_t base_address,
                                      const std::string& id,
                                      const FilePath& filename)
    : base_address(base_address), id(id), filename(filename) {}

StackSamplingProfiler::Module::~Module() = default;

// StackSamplingProfiler::Frame -----------------------------------------------

StackSamplingProfiler::Frame::Frame(uintptr_t instruction_pointer,
                                    size_t module_index)
    : instruction_pointer(instruction_pointer), module_index(module_index) {}

StackSamplingProfiler::Frame::~Frame() = default;

StackSamplingProfiler::Frame::Frame()
    : instruction_pointer(0), module_index(kUnknownModuleIndex) {
}

// StackSamplingProfiler::Sample ----------------------------------------------

StackSamplingProfiler::Sample::Sample() = default;

StackSamplingProfiler::Sample::Sample(const Sample& sample) = default;

StackSamplingProfiler::Sample::~Sample() = default;

StackSamplingProfiler::Sample::Sample(const Frame& frame) {
  frames.push_back(std::move(frame));
}

StackSamplingProfiler::Sample::Sample(const std::vector<Frame>& frames)
    : frames(frames) {}

// StackSamplingProfiler::CallStackProfile ------------------------------------

StackSamplingProfiler::CallStackProfile::CallStackProfile() = default;

StackSamplingProfiler::CallStackProfile::CallStackProfile(
    CallStackProfile&& other) = default;

StackSamplingProfiler::CallStackProfile::~CallStackProfile() = default;

StackSamplingProfiler::CallStackProfile&
StackSamplingProfiler::CallStackProfile::operator=(CallStackProfile&& other) =
    default;

StackSamplingProfiler::CallStackProfile
StackSamplingProfiler::CallStackProfile::CopyForTesting() const {
  return CallStackProfile(*this);
}

StackSamplingProfiler::CallStackProfile::CallStackProfile(
    const CallStackProfile& other) = default;

// StackSamplingProfiler::SamplingThread --------------------------------------

class StackSamplingProfiler::SamplingThread : public Thread {
 public:
  class TestAPI {
   public:
    // Reset the existing sampler. This will unfortunately create the object
    // unnecessarily if it doesn't already exist but there's no way around that.
    static void Reset();

    // Disables inherent idle-shutdown behavior.
    static void DisableIdleShutdown();

    // Begins an idle shutdown as if the idle-timer had expired and wait for
    // it to execute. Since the timer would have only been started at a time
    // when the sampling thread actually was idle, this must be called only
    // when it is known that there are no active sampling threads. If
    // |simulate_intervening_add| is true then, when executed, the shutdown
    // task will believe that a new collection has been added since it was
    // posted.
    static void ShutdownAssumingIdle(bool simulate_intervening_add);

   private:
    // Calls the sampling threads ShutdownTask and then signals an event.
    static void ShutdownTaskAndSignalEvent(SamplingThread* sampler,
                                           int add_events,
                                           WaitableEvent* event);
  };

  struct CollectionContext {
    CollectionContext(int profiler_id,
                      PlatformThreadId target,
                      const SamplingParams& params,
                      const CompletedCallback& callback,
                      WaitableEvent* finished,
                      std::unique_ptr<NativeStackSampler> sampler)
        : profiler_id(profiler_id),
          target(target),
          params(params),
          callback(callback),
          finished(finished),
          native_sampler(std::move(sampler)) {}
    ~CollectionContext() = default;

    // An identifier for the profiler associated with this collection, used to
    // uniquely identify the collection to outside interests.
    const int profiler_id;

    const PlatformThreadId target;     // ID of The thread being sampled.
    const SamplingParams params;       // Information about how to sample.
    const CompletedCallback callback;  // Callback made when sampling complete.
    WaitableEvent* const finished;     // Signaled when all sampling complete.

    // Platform-specific module that does the actual sampling.
    std::unique_ptr<NativeStackSampler> native_sampler;

    // The absolute time for the next sample.
    Time next_sample_time;

    // The time that a profile was started, for calculating the total duration.
    Time profile_start_time;

    // Counters that indicate the current position along the acquisition.
    int burst = 0;
    int sample = 0;

    // The collected stack samples. The active profile is always at the back().
    CallStackProfiles profiles;

    // Sequence number for generating new profiler ids.
    static AtomicSequenceNumber next_profiler_id;
  };

  // Gets the single instance of this class.
  static SamplingThread* GetInstance();

  // Adds a new CollectionContext to the thread. This can be called externally
  // from any thread. This returns an ID that can later be used to stop
  // the sampling.
  int Add(std::unique_ptr<CollectionContext> collection);

  // Removes an active collection based on its ID, forcing it to run its
  // callback if any data has been collected. This can be called externally
  // from any thread.
  void Remove(int id);

 private:
  friend class TestAPI;
  friend struct DefaultSingletonTraits<SamplingThread>;

  // The different states in which the sampling-thread can be.
  enum ThreadExecutionState {
    // The thread is not running because it has never been started. It will be
    // started when a sampling request is received.
    NOT_STARTED,

    // The thread is running and processing tasks. This is the state when any
    // sampling requests are active and during the "idle" period afterward
    // before the thread is stopped.
    RUNNING,

    // Once all sampling requests have finished and the "idle" period has
    // expired, the thread will be set to this state and its shutdown
    // initiated. A call to Stop() must be made to ensure the previous thread
    // has completely exited before calling Start() and moving back to the
    // RUNNING state.
    EXITING,
  };

  SamplingThread();
  ~SamplingThread() override;

  // Get task runner that is usable from the outside.
  scoped_refptr<SingleThreadTaskRunner> GetOrCreateTaskRunnerForAdd();
  scoped_refptr<SingleThreadTaskRunner> GetTaskRunner(
      ThreadExecutionState* out_state);

  // Get task runner that is usable from the sampling thread itself.
  scoped_refptr<SingleThreadTaskRunner> GetTaskRunnerOnSamplingThread();

  // Finishes a collection and reports collected data via callback. Returns
  // the new collection params, if a new collection should be started. The
  // collection's |finished| waitable event will be signalled if no new params
  // are available or |allow_collection_restart| is false. The |collection|
  // should already have been removed from |active_collections_| by the caller,
  // as this is needed to avoid flakyness in unit tests.
  Optional<SamplingParams> FinishCollection(CollectionContext* collection,
                                            bool allow_collection_restart);

  // Records a single sample of a collection.
  void RecordSample(CollectionContext* collection);

  // Check if the sampling thread is idle and begin a shutdown if it is.
  void ScheduleShutdownIfIdle();

  // These methods are tasks that get posted to the internal message queue.
  void AddCollectionTask(std::unique_ptr<CollectionContext> collection);
  void RemoveCollectionTask(int id);
  void PerformCollectionTask(int id);
  void ShutdownTask(int add_events);

  // Updates the |next_sample_time| time based on configured parameters.
  // Returns true if there is a next sample or false if sampling is complete.
  bool UpdateNextSampleTime(CollectionContext* collection);

  // Thread:
  void CleanUp() override;

  // A stack-buffer used by the native sampler for its work. This buffer can
  // be re-used for multiple native sampler objects so long as the API calls
  // that take it are not called concurrently.
  std::unique_ptr<NativeStackSampler::StackBuffer> stack_buffer_;

  // A map of IDs to collection contexts. Because this class is a singleton
  // that is never destroyed, context objects will never be destructed except
  // by explicit action. Thus, it's acceptable to pass unretained pointers
  // to these objects when posting tasks.
  std::map<int, std::unique_ptr<CollectionContext>> active_collections_;

  // State maintained about the current execution (or non-execution) of
  // the thread. This state must always be accessed while holding the
  // lock. A copy of the task-runner is maintained here for use by any
  // calling thread; this is necessary because Thread's accessor for it is
  // not itself thread-safe. The lock is also used to order calls to the
  // Thread API (Start, Stop, StopSoon, & DetachFromSequence) so that
  // multiple threads may make those calls.
  Lock thread_execution_state_lock_;  // Protects all thread_execution_state_*
  ThreadExecutionState thread_execution_state_ = NOT_STARTED;
  scoped_refptr<SingleThreadTaskRunner> thread_execution_state_task_runner_;
  bool thread_execution_state_disable_idle_shutdown_for_testing_ = false;

  // A counter that notes adds of new collection requests. It is incremented
  // when changes occur so that delayed shutdown tasks are able to detect if
  // samething new has happened while it was waiting. Like all "execution_state"
  // vars, this must be accessed while holding |thread_execution_state_lock_|.
  int thread_execution_state_add_events_ = 0;

  DISALLOW_COPY_AND_ASSIGN(SamplingThread);
};

// static
void StackSamplingProfiler::SamplingThread::TestAPI::Reset() {
  SamplingThread* sampler = SamplingThread::GetInstance();

  ThreadExecutionState state;
  {
    AutoLock lock(sampler->thread_execution_state_lock_);
    state = sampler->thread_execution_state_;
    DCHECK(sampler->active_collections_.empty());
  }

  // Stop the thread and wait for it to exit. This has to be done through by
  // the thread itself because it has taken ownership of its own lifetime.
  if (state == RUNNING) {
    ShutdownAssumingIdle(false);
    state = EXITING;
  }
  // Make sure thread is cleaned up since state will be reset to NOT_STARTED.
  if (state == EXITING)
    sampler->Stop();

  // Reset internal variables to the just-initialized state.
  {
    AutoLock lock(sampler->thread_execution_state_lock_);
    sampler->thread_execution_state_ = NOT_STARTED;
    sampler->thread_execution_state_task_runner_ = nullptr;
    sampler->thread_execution_state_disable_idle_shutdown_for_testing_ = false;
    sampler->thread_execution_state_add_events_ = 0;
  }
}

// static
void StackSamplingProfiler::SamplingThread::TestAPI::DisableIdleShutdown() {
  SamplingThread* sampler = SamplingThread::GetInstance();

  {
    AutoLock lock(sampler->thread_execution_state_lock_);
    sampler->thread_execution_state_disable_idle_shutdown_for_testing_ = true;
  }
}

// static
void StackSamplingProfiler::SamplingThread::TestAPI::ShutdownAssumingIdle(
    bool simulate_intervening_add) {
  SamplingThread* sampler = SamplingThread::GetInstance();

  ThreadExecutionState state;
  scoped_refptr<SingleThreadTaskRunner> task_runner =
      sampler->GetTaskRunner(&state);
  DCHECK_EQ(RUNNING, state);
  DCHECK(task_runner);

  int add_events;
  {
    AutoLock lock(sampler->thread_execution_state_lock_);
    add_events = sampler->thread_execution_state_add_events_;
    if (simulate_intervening_add)
      ++sampler->thread_execution_state_add_events_;
  }

  WaitableEvent executed(WaitableEvent::ResetPolicy::MANUAL,
                         WaitableEvent::InitialState::NOT_SIGNALED);
  // PostTaskAndReply won't work because thread and associated message-loop may
  // be shut down.
  task_runner->PostTask(
      FROM_HERE, BindOnce(&ShutdownTaskAndSignalEvent, Unretained(sampler),
                          add_events, Unretained(&executed)));
  executed.Wait();
}

// static
void StackSamplingProfiler::SamplingThread::TestAPI::ShutdownTaskAndSignalEvent(
    SamplingThread* sampler,
    int add_events,
    WaitableEvent* event) {
  sampler->ShutdownTask(add_events);
  event->Signal();
}

AtomicSequenceNumber
    StackSamplingProfiler::SamplingThread::CollectionContext::next_profiler_id;

StackSamplingProfiler::SamplingThread::SamplingThread()
    : Thread("StackSamplingProfiler") {}

StackSamplingProfiler::SamplingThread::~SamplingThread() = default;

StackSamplingProfiler::SamplingThread*
StackSamplingProfiler::SamplingThread::GetInstance() {
  return Singleton<SamplingThread, LeakySingletonTraits<SamplingThread>>::get();
}

int StackSamplingProfiler::SamplingThread::Add(
    std::unique_ptr<CollectionContext> collection) {
  // This is not to be run on the sampling thread.

  int id = collection->profiler_id;
  scoped_refptr<SingleThreadTaskRunner> task_runner =
      GetOrCreateTaskRunnerForAdd();

  task_runner->PostTask(
      FROM_HERE, BindOnce(&SamplingThread::AddCollectionTask, Unretained(this),
                          Passed(&collection)));

  return id;
}

void StackSamplingProfiler::SamplingThread::Remove(int id) {
  // This is not to be run on the sampling thread.

  ThreadExecutionState state;
  scoped_refptr<SingleThreadTaskRunner> task_runner = GetTaskRunner(&state);
  if (state != RUNNING)
    return;
  DCHECK(task_runner);

  // This can fail if the thread were to exit between acquisition of the task
  // runner above and the call below. In that case, however, everything has
  // stopped so there's no need to try to stop it.
  task_runner->PostTask(
      FROM_HERE,
      BindOnce(&SamplingThread::RemoveCollectionTask, Unretained(this), id));
}

scoped_refptr<SingleThreadTaskRunner>
StackSamplingProfiler::SamplingThread::GetOrCreateTaskRunnerForAdd() {
  AutoLock lock(thread_execution_state_lock_);

  // The increment of the "add events" count is why this method is to be only
  // called from "add".
  ++thread_execution_state_add_events_;

  if (thread_execution_state_ == RUNNING) {
    DCHECK(thread_execution_state_task_runner_);
    // This shouldn't be called from the sampling thread as it's inefficient.
    // Use GetTaskRunnerOnSamplingThread() instead.
    DCHECK_NE(GetThreadId(), PlatformThread::CurrentId());
    return thread_execution_state_task_runner_;
  }

  if (thread_execution_state_ == EXITING) {
    // The previous instance has only been partially cleaned up. It is necessary
    // to call Stop() before Start().
    Stop();
  }

  DCHECK(!stack_buffer_);
  stack_buffer_ = NativeStackSampler::CreateStackBuffer();

  // The thread is not running. Start it and get associated runner. The task-
  // runner has to be saved for future use because though it can be used from
  // any thread, it can be acquired via task_runner() only on the created
  // thread and the thread that creates it (i.e. this thread) for thread-safety
  // reasons which are alleviated in SamplingThread by gating access to it with
  // the |thread_execution_state_lock_|.
  Start();
  thread_execution_state_ = RUNNING;
  thread_execution_state_task_runner_ = Thread::task_runner();

  // Detach the sampling thread from the "sequence" (i.e. thread) that
  // started it so that it can be self-managed or stopped by another thread.
  DetachFromSequence();

  return thread_execution_state_task_runner_;
}

scoped_refptr<SingleThreadTaskRunner>
StackSamplingProfiler::SamplingThread::GetTaskRunner(
    ThreadExecutionState* out_state) {
  AutoLock lock(thread_execution_state_lock_);
  if (out_state)
    *out_state = thread_execution_state_;
  if (thread_execution_state_ == RUNNING) {
    // This shouldn't be called from the sampling thread as it's inefficient.
    // Use GetTaskRunnerOnSamplingThread() instead.
    DCHECK_NE(GetThreadId(), PlatformThread::CurrentId());
    DCHECK(thread_execution_state_task_runner_);
  } else {
    DCHECK(!thread_execution_state_task_runner_);
  }

  return thread_execution_state_task_runner_;
}

scoped_refptr<SingleThreadTaskRunner>
StackSamplingProfiler::SamplingThread::GetTaskRunnerOnSamplingThread() {
  // This should be called only from the sampling thread as it has limited
  // accessibility.
  DCHECK_EQ(GetThreadId(), PlatformThread::CurrentId());

  return Thread::task_runner();
}

Optional<StackSamplingProfiler::SamplingParams>
StackSamplingProfiler::SamplingThread::FinishCollection(
    CollectionContext* collection,
    bool allow_collection_restart) {
  DCHECK_EQ(GetThreadId(), PlatformThread::CurrentId());
  DCHECK_EQ(0u, active_collections_.count(collection->profiler_id));

  // If there is no duration for the final profile (because it was stopped),
  // calculate it now.
  if (!collection->profiles.empty() &&
      collection->profiles.back().profile_duration == TimeDelta()) {
    collection->profiles.back().profile_duration =
        Time::Now() - collection->profile_start_time +
        collection->params.sampling_interval;
  }

  // Extract some information so callback and event-signalling can still be
  // done after the collection has been removed from the list of "active" ones.
  // This allows the the controlling object (and tests using it) to be confident
  // that collection is fully finished when those things occur.
  const CompletedCallback callback = collection->callback;
  CallStackProfiles profiles = std::move(collection->profiles);
  WaitableEvent* finished = collection->finished;

  // Run the associated callback, passing the collected profiles.
  Optional<SamplingParams> new_params = callback.Run(std::move(profiles));
  if (!allow_collection_restart)
    new_params.reset();

  // Signal that this collection is finished if it shouldn't be rescheduled.
  if (!new_params.has_value())
    finished->Signal();

  return new_params;
}

void StackSamplingProfiler::SamplingThread::RecordSample(
    CollectionContext* collection) {
  DCHECK_EQ(GetThreadId(), PlatformThread::CurrentId());
  DCHECK(collection->native_sampler);

  // If this is the first sample of a burst, a new Profile needs to be created
  // and filled.
  if (collection->sample == 0) {
    collection->profiles.push_back(CallStackProfile());
    CallStackProfile& profile = collection->profiles.back();
    profile.sampling_period = collection->params.sampling_interval;
    collection->profile_start_time = Time::Now();
    collection->native_sampler->ProfileRecordingStarting(&profile.modules);
  }

  // The currently active profile being captured.
  CallStackProfile& profile = collection->profiles.back();

  // Record a single sample.
  profile.samples.push_back(Sample());
  collection->native_sampler->RecordStackSample(stack_buffer_.get(),
                                                &profile.samples.back());

  // If this is the last sample of a burst, record the total time.
  if (collection->sample == collection->params.samples_per_burst - 1) {
    profile.profile_duration = Time::Now() - collection->profile_start_time +
                               collection->params.sampling_interval;
    collection->native_sampler->ProfileRecordingStopped(stack_buffer_.get());
  }
}

void StackSamplingProfiler::SamplingThread::ScheduleShutdownIfIdle() {
  DCHECK_EQ(GetThreadId(), PlatformThread::CurrentId());

  if (!active_collections_.empty())
    return;

  int add_events;
  {
    AutoLock lock(thread_execution_state_lock_);
    if (thread_execution_state_disable_idle_shutdown_for_testing_)
      return;
    add_events = thread_execution_state_add_events_;
  }

  GetTaskRunnerOnSamplingThread()->PostDelayedTask(
      FROM_HERE,
      BindOnce(&SamplingThread::ShutdownTask, Unretained(this), add_events),
      TimeDelta::FromSeconds(60));
}

void StackSamplingProfiler::SamplingThread::AddCollectionTask(
    std::unique_ptr<CollectionContext> collection) {
  DCHECK_EQ(GetThreadId(), PlatformThread::CurrentId());

  const int profiler_id = collection->profiler_id;
  const TimeDelta initial_delay = collection->params.initial_delay;

  active_collections_.insert(
      std::make_pair(profiler_id, std::move(collection)));

  GetTaskRunnerOnSamplingThread()->PostDelayedTask(
      FROM_HERE,
      BindOnce(&SamplingThread::PerformCollectionTask, Unretained(this),
               profiler_id),
      initial_delay);

  // Another increment of "add events" serves to invalidate any pending
  // shutdown tasks that may have been initiated between the Add() and this
  // task running.
  {
    AutoLock lock(thread_execution_state_lock_);
    ++thread_execution_state_add_events_;
  }
}

void StackSamplingProfiler::SamplingThread::RemoveCollectionTask(int id) {
  DCHECK_EQ(GetThreadId(), PlatformThread::CurrentId());

  auto found = active_collections_.find(id);
  if (found == active_collections_.end())
    return;

  // Remove |collection| from |active_collections_|.
  std::unique_ptr<CollectionContext> collection = std::move(found->second);
  size_t count = active_collections_.erase(id);
  DCHECK_EQ(1U, count);

  FinishCollection(collection.get(), false);
  ScheduleShutdownIfIdle();
}

void StackSamplingProfiler::SamplingThread::PerformCollectionTask(int id) {
  DCHECK_EQ(GetThreadId(), PlatformThread::CurrentId());

  auto found = active_collections_.find(id);

  // The task won't be found if it has been stopped.
  if (found == active_collections_.end())
    return;

  CollectionContext* collection = found->second.get();

  // Handle first-run with no "next time".
  if (collection->next_sample_time == Time())
    collection->next_sample_time = Time::Now();

  // Do the collection of a single sample.
  RecordSample(collection);

  // Update the time of the next sample recording.
  const bool collection_finished = !UpdateNextSampleTime(collection);
  if (!collection_finished) {
    bool success = GetTaskRunnerOnSamplingThread()->PostDelayedTask(
        FROM_HERE,
        BindOnce(&SamplingThread::PerformCollectionTask, Unretained(this), id),
        std::max(collection->next_sample_time - Time::Now(), TimeDelta()));
    DCHECK(success);
    return;
  }

  // Take ownership of |collection| and remove it from the map. If collection is
  // to be restarted, a new collection task will be added below.
  std::unique_ptr<CollectionContext> owned_collection =
      std::move(found->second);
  size_t count = active_collections_.erase(id);
  DCHECK_EQ(1U, count);

  // All capturing has completed so finish the collection. If no new params
  // are returned, a new collection should not be started.
  Optional<SamplingParams> new_params = FinishCollection(collection, true);
  if (!new_params.has_value()) {
    // By not adding it to the task queue, the collection will "expire" (i.e.
    // no further work will be done).
    ScheduleShutdownIfIdle();
    return;
  }

  // Restart the collection with the new params. Keep the same id so the
  // Stop() operation continues to work.
  auto new_collection = std::make_unique<SamplingThread::CollectionContext>(
      id, collection->target, new_params.value(), collection->callback,
      collection->finished, std::move(collection->native_sampler));
  AddCollectionTask(std::move(new_collection));
}

void StackSamplingProfiler::SamplingThread::ShutdownTask(int add_events) {
  DCHECK_EQ(GetThreadId(), PlatformThread::CurrentId());

  // Holding this lock ensures that any attempt to start another job will
  // get postponed until |thread_execution_state_| is updated, thus eliminating
  // the race in starting a new thread while the previous one is exiting.
  AutoLock lock(thread_execution_state_lock_);

  // If the current count of creation requests doesn't match the passed count
  // then other tasks have been created since this was posted. Abort shutdown.
  if (thread_execution_state_add_events_ != add_events)
    return;

  // There can be no new AddCollectionTasks at this point because creating
  // those always increments "add events". There may be other requests, like
  // Remove, but it's okay to schedule the thread to stop once they've been
  // executed (i.e. "soon").
  DCHECK(active_collections_.empty());
  StopSoon();

  // StopSoon will have set the owning sequence (again) so it must be detached
  // (again) in order for Stop/Start to be called (again) should more work
  // come in. Holding the |thread_execution_state_lock_| ensures the necessary
  // happens-after with regard to this detach and future Thread API calls.
  DetachFromSequence();

  // Set the thread_state variable so the thread will be restarted when new
  // work comes in. Remove the |thread_execution_state_task_runner_| to avoid
  // confusion.
  thread_execution_state_ = EXITING;
  thread_execution_state_task_runner_ = nullptr;
  stack_buffer_.reset();
}

bool StackSamplingProfiler::SamplingThread::UpdateNextSampleTime(
    CollectionContext* collection) {
  // This will keep a consistent average interval between samples but will
  // result in constant series of acquisitions, thus nearly locking out the
  // target thread, if the interval is smaller than the time it takes to
  // actually acquire the sample. Anything sampling that quickly is going
  // to be a problem anyway so don't worry about it.
  if (++collection->sample < collection->params.samples_per_burst) {
    collection->next_sample_time += collection->params.sampling_interval;
    return true;
  }

  if (++collection->burst < collection->params.bursts) {
    collection->sample = 0;
    collection->next_sample_time += collection->params.burst_interval;
    return true;
  }

  return false;
}

void StackSamplingProfiler::SamplingThread::CleanUp() {
  DCHECK_EQ(GetThreadId(), PlatformThread::CurrentId());

  // There should be no collections remaining when the thread stops.
  DCHECK(active_collections_.empty());

  // Let the parent clean up.
  Thread::CleanUp();
}

// StackSamplingProfiler ------------------------------------------------------

// static
void StackSamplingProfiler::TestAPI::Reset() {
  SamplingThread::TestAPI::Reset();
  ResetAnnotations();
}

// static
void StackSamplingProfiler::TestAPI::ResetAnnotations() {
  subtle::NoBarrier_Store(&process_milestones_, 0u);
}

// static
bool StackSamplingProfiler::TestAPI::IsSamplingThreadRunning() {
  return SamplingThread::GetInstance()->IsRunning();
}

// static
void StackSamplingProfiler::TestAPI::DisableIdleShutdown() {
  SamplingThread::TestAPI::DisableIdleShutdown();
}

// static
void StackSamplingProfiler::TestAPI::PerformSamplingThreadIdleShutdown(
    bool simulate_intervening_start) {
  SamplingThread::TestAPI::ShutdownAssumingIdle(simulate_intervening_start);
}

subtle::Atomic32 StackSamplingProfiler::process_milestones_ = 0;

StackSamplingProfiler::StackSamplingProfiler(
    const SamplingParams& params,
    const CompletedCallback& callback,
    NativeStackSamplerTestDelegate* test_delegate)
    : StackSamplingProfiler(base::PlatformThread::CurrentId(),
                            params,
                            callback,
                            test_delegate) {}

StackSamplingProfiler::StackSamplingProfiler(
    PlatformThreadId thread_id,
    const SamplingParams& params,
    const CompletedCallback& callback,
    NativeStackSamplerTestDelegate* test_delegate)
    : thread_id_(thread_id),
      params_(params),
      completed_callback_(callback),
      // The event starts "signaled" so code knows it's safe to start thread
      // and "manual" so that it can be waited in multiple places.
      profiling_inactive_(WaitableEvent::ResetPolicy::MANUAL,
                          WaitableEvent::InitialState::SIGNALED),
      profiler_id_(NULL_PROFILER_ID),
      test_delegate_(test_delegate) {}

StackSamplingProfiler::~StackSamplingProfiler() {
  // Stop returns immediately but the shutdown runs asynchronously. There is a
  // non-zero probability that one more sample will be taken after this call
  // returns.
  Stop();

  // The behavior of sampling a thread that has exited is undefined and could
  // cause Bad Things(tm) to occur. The safety model provided by this class is
  // that an instance of this object is expected to live at least as long as
  // the thread it is sampling. However, because the sampling is performed
  // asynchronously by the SamplingThread, there is no way to guarantee this
  // is true without waiting for it to signal that it has finished.
  //
  // The wait time should, at most, be only as long as it takes to collect one
  // sample (~200us) or none at all if sampling has already completed.
  ThreadRestrictions::ScopedAllowWait allow_wait;
  profiling_inactive_.Wait();
}

void StackSamplingProfiler::Start() {
  if (completed_callback_.is_null())
    return;

  std::unique_ptr<NativeStackSampler> native_sampler =
      NativeStackSampler::Create(thread_id_, &RecordAnnotations,
                                 test_delegate_);

  if (!native_sampler)
    return;

  // Wait for profiling to be "inactive", then reset it for the upcoming run.
  profiling_inactive_.Wait();
  profiling_inactive_.Reset();

  DCHECK_EQ(NULL_PROFILER_ID, profiler_id_);
  profiler_id_ = SamplingThread::GetInstance()->Add(
      std::make_unique<SamplingThread::CollectionContext>(
          SamplingThread::CollectionContext::next_profiler_id.GetNext(),
          thread_id_, params_, completed_callback_, &profiling_inactive_,
          std::move(native_sampler)));
  DCHECK_NE(NULL_PROFILER_ID, profiler_id_);
}

void StackSamplingProfiler::Stop() {
  SamplingThread::GetInstance()->Remove(profiler_id_);
  profiler_id_ = NULL_PROFILER_ID;
}

// static
void StackSamplingProfiler::SetProcessMilestone(int milestone) {
  DCHECK_LE(0, milestone);
  DCHECK_GT(static_cast<int>(sizeof(process_milestones_) * 8), milestone);
  DCHECK_EQ(0, subtle::NoBarrier_Load(&process_milestones_) & (1 << milestone));
  ChangeAtomicFlags(&process_milestones_, 1 << milestone, 0);
}

// static
void StackSamplingProfiler::RecordAnnotations(Sample* sample) {
  // The code inside this method must not do anything that could acquire a
  // mutex, including allocating memory (which includes LOG messages) because
  // that mutex could be held by a stopped thread, thus resulting in deadlock.
  sample->process_milestones = subtle::NoBarrier_Load(&process_milestones_);
}

// StackSamplingProfiler::Frame global functions ------------------------------

bool operator==(const StackSamplingProfiler::Module& a,
                const StackSamplingProfiler::Module& b) {
  return a.base_address == b.base_address && a.id == b.id &&
      a.filename == b.filename;
}

bool operator==(const StackSamplingProfiler::Sample& a,
                const StackSamplingProfiler::Sample& b) {
  return a.process_milestones == b.process_milestones && a.frames == b.frames;
}

bool operator!=(const StackSamplingProfiler::Sample& a,
                const StackSamplingProfiler::Sample& b) {
  return !(a == b);
}

bool operator<(const StackSamplingProfiler::Sample& a,
               const StackSamplingProfiler::Sample& b) {
  if (a.process_milestones < b.process_milestones)
    return true;
  if (a.process_milestones > b.process_milestones)
    return false;

  return a.frames < b.frames;
}

bool operator==(const StackSamplingProfiler::Frame &a,
                const StackSamplingProfiler::Frame &b) {
  return a.instruction_pointer == b.instruction_pointer &&
      a.module_index == b.module_index;
}

bool operator<(const StackSamplingProfiler::Frame &a,
               const StackSamplingProfiler::Frame &b) {
  return (a.module_index < b.module_index) ||
      (a.module_index == b.module_index &&
       a.instruction_pointer < b.instruction_pointer);
}

}  // namespace base
