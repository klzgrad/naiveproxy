// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/synchronization/waitable_event_watcher.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

namespace {

// The message loops on which each waitable event timer should be tested.
const MessageLoop::Type testing_message_loops[] = {
  MessageLoop::TYPE_DEFAULT,
  MessageLoop::TYPE_IO,
#if !defined(OS_IOS)  // iOS does not allow direct running of the UI loop.
  MessageLoop::TYPE_UI,
#endif
};

void QuitWhenSignaled(WaitableEvent* event) {
  RunLoop::QuitCurrentWhenIdleDeprecated();
}

class DecrementCountContainer {
 public:
  explicit DecrementCountContainer(int* counter) : counter_(counter) {}
  void OnWaitableEventSignaled(WaitableEvent* object) {
    // NOTE: |object| may be already deleted.
    --(*counter_);
  }

 private:
  int* counter_;
};

}  // namespace

class WaitableEventWatcherTest
    : public testing::TestWithParam<MessageLoop::Type> {};

TEST_P(WaitableEventWatcherTest, BasicSignalManual) {
  MessageLoop message_loop(GetParam());

  // A manual-reset event that is not yet signaled.
  WaitableEvent event(WaitableEvent::ResetPolicy::MANUAL,
                      WaitableEvent::InitialState::NOT_SIGNALED);

  WaitableEventWatcher watcher;
  watcher.StartWatching(&event, BindOnce(&QuitWhenSignaled),
                        SequencedTaskRunnerHandle::Get());

  event.Signal();

  RunLoop().Run();

  EXPECT_TRUE(event.IsSignaled());
}

TEST_P(WaitableEventWatcherTest, BasicSignalAutomatic) {
  MessageLoop message_loop(GetParam());

  WaitableEvent event(WaitableEvent::ResetPolicy::AUTOMATIC,
                      WaitableEvent::InitialState::NOT_SIGNALED);

  WaitableEventWatcher watcher;
  watcher.StartWatching(&event, BindOnce(&QuitWhenSignaled),
                        SequencedTaskRunnerHandle::Get());

  event.Signal();

  RunLoop().Run();

  // The WaitableEventWatcher consumes the event signal.
  EXPECT_FALSE(event.IsSignaled());
}

TEST_P(WaitableEventWatcherTest, BasicCancel) {
  MessageLoop message_loop(GetParam());

  // A manual-reset event that is not yet signaled.
  WaitableEvent event(WaitableEvent::ResetPolicy::MANUAL,
                      WaitableEvent::InitialState::NOT_SIGNALED);

  WaitableEventWatcher watcher;

  watcher.StartWatching(&event, BindOnce(&QuitWhenSignaled),
                        SequencedTaskRunnerHandle::Get());

  watcher.StopWatching();
}

TEST_P(WaitableEventWatcherTest, CancelAfterSet) {
  MessageLoop message_loop(GetParam());

  // A manual-reset event that is not yet signaled.
  WaitableEvent event(WaitableEvent::ResetPolicy::MANUAL,
                      WaitableEvent::InitialState::NOT_SIGNALED);

  WaitableEventWatcher watcher;

  int counter = 1;
  DecrementCountContainer delegate(&counter);
  WaitableEventWatcher::EventCallback callback = BindOnce(
      &DecrementCountContainer::OnWaitableEventSignaled, Unretained(&delegate));
  watcher.StartWatching(&event, std::move(callback),
                        SequencedTaskRunnerHandle::Get());

  event.Signal();

  // Let the background thread do its business
  PlatformThread::Sleep(TimeDelta::FromMilliseconds(30));

  watcher.StopWatching();

  RunLoop().RunUntilIdle();

  // Our delegate should not have fired.
  EXPECT_EQ(1, counter);
}

TEST_P(WaitableEventWatcherTest, OutlivesMessageLoop) {
  // Simulate a MessageLoop that dies before an WaitableEventWatcher.  This
  // ordinarily doesn't happen when people use the Thread class, but it can
  // happen when people use the Singleton pattern or atexit.
  WaitableEvent event(WaitableEvent::ResetPolicy::MANUAL,
                      WaitableEvent::InitialState::NOT_SIGNALED);
  {
    std::unique_ptr<WaitableEventWatcher> watcher;
    {
      MessageLoop message_loop(GetParam());
      watcher = std::make_unique<WaitableEventWatcher>();

      watcher->StartWatching(&event, BindOnce(&QuitWhenSignaled),
                             SequencedTaskRunnerHandle::Get());
    }
  }
}

TEST_P(WaitableEventWatcherTest, SignaledAtStartManual) {
  MessageLoop message_loop(GetParam());

  WaitableEvent event(WaitableEvent::ResetPolicy::MANUAL,
                      WaitableEvent::InitialState::SIGNALED);

  WaitableEventWatcher watcher;
  watcher.StartWatching(&event, BindOnce(&QuitWhenSignaled),
                        SequencedTaskRunnerHandle::Get());

  RunLoop().Run();

  EXPECT_TRUE(event.IsSignaled());
}

TEST_P(WaitableEventWatcherTest, SignaledAtStartAutomatic) {
  MessageLoop message_loop(GetParam());

  WaitableEvent event(WaitableEvent::ResetPolicy::AUTOMATIC,
                      WaitableEvent::InitialState::SIGNALED);

  WaitableEventWatcher watcher;
  watcher.StartWatching(&event, BindOnce(&QuitWhenSignaled),
                        SequencedTaskRunnerHandle::Get());

  RunLoop().Run();

  // The watcher consumes the event signal.
  EXPECT_FALSE(event.IsSignaled());
}

TEST_P(WaitableEventWatcherTest, StartWatchingInCallback) {
  MessageLoop message_loop(GetParam());

  WaitableEvent event(WaitableEvent::ResetPolicy::MANUAL,
                      WaitableEvent::InitialState::NOT_SIGNALED);

  WaitableEventWatcher watcher;
  watcher.StartWatching(
      &event,
      BindOnce(
          [](WaitableEventWatcher* watcher, WaitableEvent* event) {
            // |event| is manual, so the second watcher will run
            // immediately.
            watcher->StartWatching(event, BindOnce(&QuitWhenSignaled),
                                   SequencedTaskRunnerHandle::Get());
          },
          &watcher),
      SequencedTaskRunnerHandle::Get());

  event.Signal();

  RunLoop().Run();
}

TEST_P(WaitableEventWatcherTest, MultipleWatchersManual) {
  MessageLoop message_loop(GetParam());

  WaitableEvent event(WaitableEvent::ResetPolicy::MANUAL,
                      WaitableEvent::InitialState::NOT_SIGNALED);

  int counter1 = 0;
  int counter2 = 0;

  auto callback = [](RunLoop* run_loop, int* counter, WaitableEvent* event) {
    ++(*counter);
    run_loop->QuitWhenIdle();
  };

  RunLoop run_loop;

  WaitableEventWatcher watcher1;
  watcher1.StartWatching(
      &event, BindOnce(callback, Unretained(&run_loop), Unretained(&counter1)),
      SequencedTaskRunnerHandle::Get());

  WaitableEventWatcher watcher2;
  watcher2.StartWatching(
      &event, BindOnce(callback, Unretained(&run_loop), Unretained(&counter2)),
      SequencedTaskRunnerHandle::Get());

  event.Signal();
  run_loop.Run();

  EXPECT_EQ(1, counter1);
  EXPECT_EQ(1, counter2);
  EXPECT_TRUE(event.IsSignaled());
}

// Tests that only one async waiter gets called back for an auto-reset event.
TEST_P(WaitableEventWatcherTest, MultipleWatchersAutomatic) {
  MessageLoop message_loop(GetParam());

  WaitableEvent event(WaitableEvent::ResetPolicy::AUTOMATIC,
                      WaitableEvent::InitialState::NOT_SIGNALED);

  int counter1 = 0;
  int counter2 = 0;

  auto callback = [](RunLoop** run_loop, int* counter, WaitableEvent* event) {
    ++(*counter);
    (*run_loop)->QuitWhenIdle();
  };

  // The same RunLoop instance cannot be Run more than once, and it is
  // undefined which watcher will get called back first. Have the callback
  // dereference this pointer to quit the loop, which will be updated on each
  // Run.
  RunLoop* current_run_loop;

  WaitableEventWatcher watcher1;
  watcher1.StartWatching(
      &event,
      BindOnce(callback, Unretained(&current_run_loop), Unretained(&counter1)),
      SequencedTaskRunnerHandle::Get());

  WaitableEventWatcher watcher2;
  watcher2.StartWatching(
      &event,
      BindOnce(callback, Unretained(&current_run_loop), Unretained(&counter2)),
      SequencedTaskRunnerHandle::Get());

  event.Signal();
  {
    RunLoop run_loop;
    current_run_loop = &run_loop;
    run_loop.Run();
  }

  // Only one of the waiters should have been signaled.
  EXPECT_TRUE((counter1 == 1) ^ (counter2 == 1));

  EXPECT_FALSE(event.IsSignaled());

  event.Signal();
  {
    RunLoop run_loop;
    current_run_loop = &run_loop;
    run_loop.Run();
  }

  EXPECT_FALSE(event.IsSignaled());

  // The other watcher should have been signaled.
  EXPECT_EQ(1, counter1);
  EXPECT_EQ(1, counter2);
}

// To help detect errors around deleting WaitableEventWatcher, an additional
// bool parameter is used to test sleeping between watching and deletion.
class WaitableEventWatcherDeletionTest
    : public testing::TestWithParam<std::tuple<MessageLoop::Type, bool>> {};

TEST_P(WaitableEventWatcherDeletionTest, DeleteUnder) {
  MessageLoop::Type message_loop_type;
  bool delay_after_delete;
  std::tie(message_loop_type, delay_after_delete) = GetParam();

  // Delete the WaitableEvent out from under the Watcher. This is explictly
  // allowed by the interface.

  MessageLoop message_loop(message_loop_type);

  {
    WaitableEventWatcher watcher;

    auto* event = new WaitableEvent(WaitableEvent::ResetPolicy::AUTOMATIC,
                                    WaitableEvent::InitialState::NOT_SIGNALED);

    watcher.StartWatching(event, BindOnce(&QuitWhenSignaled),
                          SequencedTaskRunnerHandle::Get());

    if (delay_after_delete) {
      // On Windows that sleep() improves the chance to catch some problems.
      // It postpones the dtor |watcher| (which immediately cancel the waiting)
      // and gives some time to run to a created background thread.
      // Unfortunately, that thread is under OS control and we can't
      // manipulate it directly.
      PlatformThread::Sleep(TimeDelta::FromMilliseconds(30));
    }

    delete event;
  }
}

TEST_P(WaitableEventWatcherDeletionTest, SignalAndDelete) {
  MessageLoop::Type message_loop_type;
  bool delay_after_delete;
  std::tie(message_loop_type, delay_after_delete) = GetParam();

  // Signal and immediately delete the WaitableEvent out from under the Watcher.

  MessageLoop message_loop(message_loop_type);

  {
    WaitableEventWatcher watcher;

    auto event = std::make_unique<WaitableEvent>(
        WaitableEvent::ResetPolicy::AUTOMATIC,
        WaitableEvent::InitialState::NOT_SIGNALED);

    watcher.StartWatching(event.get(), BindOnce(&QuitWhenSignaled),
                          SequencedTaskRunnerHandle::Get());
    event->Signal();
    event.reset();

    if (delay_after_delete) {
      // On Windows that sleep() improves the chance to catch some problems.
      // It postpones the dtor |watcher| (which immediately cancel the waiting)
      // and gives some time to run to a created background thread.
      // Unfortunately, that thread is under OS control and we can't
      // manipulate it directly.
      PlatformThread::Sleep(TimeDelta::FromMilliseconds(30));
    }

    // Wait for the watcher callback.
    RunLoop().Run();
  }
}

// Tests deleting the WaitableEventWatcher between signaling the event and
// when the callback should be run.
TEST_P(WaitableEventWatcherDeletionTest, DeleteWatcherBeforeCallback) {
  MessageLoop::Type message_loop_type;
  bool delay_after_delete;
  std::tie(message_loop_type, delay_after_delete) = GetParam();

  MessageLoop message_loop(message_loop_type);
  scoped_refptr<SingleThreadTaskRunner> task_runner =
      message_loop.task_runner();

  // Flag used to esnure that the |watcher_callback| never runs.
  bool did_callback = false;

  WaitableEvent event(WaitableEvent::ResetPolicy::AUTOMATIC,
                      WaitableEvent::InitialState::NOT_SIGNALED);
  auto watcher = std::make_unique<WaitableEventWatcher>();

  // Queue up a series of tasks:
  // 1. StartWatching the WaitableEvent
  // 2. Signal the event (which will result in another task getting posted to
  //    the |task_runner|)
  // 3. Delete the WaitableEventWatcher
  // 4. WaitableEventWatcher callback should run (from #2)

  WaitableEventWatcher::EventCallback watcher_callback = BindOnce(
      [](bool* did_callback, WaitableEvent*) {
        *did_callback = true;
      },
      Unretained(&did_callback));

  task_runner->PostTask(
      FROM_HERE, BindOnce(IgnoreResult(&WaitableEventWatcher::StartWatching),
                          Unretained(watcher.get()), Unretained(&event),
                          std::move(watcher_callback), task_runner));
  task_runner->PostTask(FROM_HERE,
                        BindOnce(&WaitableEvent::Signal, Unretained(&event)));
  task_runner->DeleteSoon(FROM_HERE, std::move(watcher));
  if (delay_after_delete) {
    task_runner->PostTask(FROM_HERE, BindOnce(&PlatformThread::Sleep,
                                              TimeDelta::FromMilliseconds(30)));
  }

  RunLoop().RunUntilIdle();

  EXPECT_FALSE(did_callback);
}

INSTANTIATE_TEST_CASE_P(,
                        WaitableEventWatcherTest,
                        testing::ValuesIn(testing_message_loops));

INSTANTIATE_TEST_CASE_P(
    ,
    WaitableEventWatcherDeletionTest,
    testing::Combine(testing::ValuesIn(testing_message_loops),
                     testing::Bool()));

}  // namespace base
