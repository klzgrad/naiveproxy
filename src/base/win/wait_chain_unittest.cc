// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/wait_chain.h"

#include <memory>
#include <string>

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/test/multiprocess_test.h"
#include "base/threading/simple_thread.h"
#include "base/win/win_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"

namespace base {
namespace win {

namespace {

// Appends |handle| as a command line switch.
void AppendSwitchHandle(CommandLine* command_line,
                        StringPiece switch_name,
                        HANDLE handle) {
  command_line->AppendSwitchASCII(switch_name.as_string(),
                                  UintToString(HandleToUint32(handle)));
}

// Retrieves the |handle| associated to |switch_name| from the command line.
ScopedHandle GetSwitchValueHandle(CommandLine* command_line,
                                  StringPiece switch_name) {
  std::string switch_string =
      command_line->GetSwitchValueASCII(switch_name.as_string());
  unsigned int switch_uint = 0;
  if (switch_string.empty() || !StringToUint(switch_string, &switch_uint)) {
    DLOG(ERROR) << "Missing or invalid " << switch_name << " argument.";
    return ScopedHandle();
  }
  return ScopedHandle(reinterpret_cast<HANDLE>(switch_uint));
}

// Helper function to create a mutex.
ScopedHandle CreateMutex(bool inheritable) {
  SECURITY_ATTRIBUTES security_attributes = {sizeof(SECURITY_ATTRIBUTES),
                                             nullptr, inheritable};
  return ScopedHandle(::CreateMutex(&security_attributes, FALSE, NULL));
}

// Helper function to create an event.
ScopedHandle CreateEvent(bool inheritable) {
  SECURITY_ATTRIBUTES security_attributes = {sizeof(SECURITY_ATTRIBUTES),
                                             nullptr, inheritable};
  return ScopedHandle(
      ::CreateEvent(&security_attributes, FALSE, FALSE, nullptr));
}

// Helper thread class that runs the callback then stops.
class SingleTaskThread : public SimpleThread {
 public:
  explicit SingleTaskThread(const Closure& task)
      : SimpleThread("WaitChainTest SingleTaskThread"), task_(task) {}

  void Run() override { task_.Run(); }

 private:
  Closure task_;

  DISALLOW_COPY_AND_ASSIGN(SingleTaskThread);
};

// Helper thread to cause a deadlock by acquiring 2 mutexes in a given order.
class DeadlockThread : public SimpleThread {
 public:
  DeadlockThread(HANDLE mutex_1, HANDLE mutex_2)
      : SimpleThread("WaitChainTest DeadlockThread"),
        wait_event_(CreateEvent(false)),
        mutex_acquired_event_(CreateEvent(false)),
        mutex_1_(mutex_1),
        mutex_2_(mutex_2) {}

  void Run() override {
    // Acquire the mutex then signal the main thread.
    EXPECT_EQ(WAIT_OBJECT_0, ::WaitForSingleObject(mutex_1_, INFINITE));
    EXPECT_TRUE(::SetEvent(mutex_acquired_event_.Get()));

    // Wait until both threads are holding their mutex before trying to acquire
    // the other one.
    EXPECT_EQ(WAIT_OBJECT_0,
              ::WaitForSingleObject(wait_event_.Get(), INFINITE));

    // To unblock the deadlock, one of the threads will get terminated (via
    // TerminateThread()) without releasing the mutex. This causes the other
    // thread to wake up with WAIT_ABANDONED.
    EXPECT_EQ(WAIT_ABANDONED, ::WaitForSingleObject(mutex_2_, INFINITE));
  }

  // Blocks until a mutex is acquired.
  void WaitForMutexAcquired() {
    EXPECT_EQ(WAIT_OBJECT_0,
              ::WaitForSingleObject(mutex_acquired_event_.Get(), INFINITE));
  }

  // Signal the thread to acquire the second mutex.
  void SignalToAcquireMutex() { EXPECT_TRUE(::SetEvent(wait_event_.Get())); }

  // Terminates the thread.
  bool Terminate() {
    ScopedHandle thread_handle(::OpenThread(THREAD_TERMINATE, FALSE, tid()));
    return ::TerminateThread(thread_handle.Get(), 0);
  }

 private:
  ScopedHandle wait_event_;
  ScopedHandle mutex_acquired_event_;

  // The 2 mutex to acquire.
  HANDLE mutex_1_;
  HANDLE mutex_2_;

  DISALLOW_COPY_AND_ASSIGN(DeadlockThread);
};

// Creates a thread that joins |thread_to_join| and then terminates when it
// finishes execution.
std::unique_ptr<SingleTaskThread> CreateJoiningThread(
    SimpleThread* thread_to_join) {
  std::unique_ptr<SingleTaskThread> thread(new SingleTaskThread(
      Bind(&SimpleThread::Join, Unretained(thread_to_join))));
  thread->Start();

  return thread;
}

// Creates a thread that calls WaitForSingleObject() on the handle and then
// terminates when it unblocks.
std::unique_ptr<SingleTaskThread> CreateWaitingThread(HANDLE handle) {
  std::unique_ptr<SingleTaskThread> thread(new SingleTaskThread(
      Bind(IgnoreResult(&::WaitForSingleObject), handle, INFINITE)));
  thread->Start();

  return thread;
}

// Creates a thread that blocks on |mutex_2| after acquiring |mutex_1|.
std::unique_ptr<DeadlockThread> CreateDeadlockThread(HANDLE mutex_1,
                                                     HANDLE mutex_2) {
  std::unique_ptr<DeadlockThread> thread(new DeadlockThread(mutex_1, mutex_2));
  thread->Start();

  // Wait until the first mutex is acquired before returning.
  thread->WaitForMutexAcquired();

  return thread;
}

// Child process to test the cross-process capability of the WCT api.
// This process will simulate a hang while holding a mutex that the parent
// process is waiting on.
MULTIPROCESS_TEST_MAIN(WaitChainTestProc) {
  CommandLine* command_line = CommandLine::ForCurrentProcess();

  ScopedHandle mutex = GetSwitchValueHandle(command_line, "mutex");
  CHECK(mutex.IsValid());

  ScopedHandle sync_event(GetSwitchValueHandle(command_line, "sync_event"));
  CHECK(sync_event.IsValid());

  // Acquire mutex.
  CHECK(::WaitForSingleObject(mutex.Get(), INFINITE) == WAIT_OBJECT_0);

  // Signal back to the parent process that the mutex is hold.
  CHECK(::SetEvent(sync_event.Get()));

  // Wait on a signal from the parent process before terminating.
  CHECK(::WaitForSingleObject(sync_event.Get(), INFINITE) == WAIT_OBJECT_0);

  return 0;
}

// Start a child process and passes the |mutex| and the |sync_event| to the
// command line.
Process StartChildProcess(HANDLE mutex, HANDLE sync_event) {
  CommandLine command_line = GetMultiProcessTestChildBaseCommandLine();

  AppendSwitchHandle(&command_line, "mutex", mutex);
  AppendSwitchHandle(&command_line, "sync_event", sync_event);

  LaunchOptions options;
  options.handles_to_inherit.push_back(mutex);
  options.handles_to_inherit.push_back(sync_event);
  return SpawnMultiProcessTestChild("WaitChainTestProc", command_line, options);
}

// Returns true if the |wait_chain| is an alternating sequence of thread objects
// and synchronization objects.
bool WaitChainStructureIsCorrect(const WaitChainNodeVector& wait_chain) {
  // Checks thread objects.
  for (size_t i = 0; i < wait_chain.size(); i += 2) {
    if (wait_chain[i].ObjectType != WctThreadType)
      return false;
  }

  // Check synchronization objects.
  for (size_t i = 1; i < wait_chain.size(); i += 2) {
    if (wait_chain[i].ObjectType == WctThreadType)
      return false;
  }
  return true;
}

// Returns true if the |wait_chain| goes through more than 1 process.
bool WaitChainIsCrossProcess(const WaitChainNodeVector& wait_chain) {
  if (wait_chain.size() == 0)
    return false;

  // Just check that the process id changes somewhere in the chain.
  // Note: ThreadObjects are every 2 nodes.
  DWORD first_process = wait_chain[0].ThreadObject.ProcessId;
  for (size_t i = 2; i < wait_chain.size(); i += 2) {
    if (first_process != wait_chain[i].ThreadObject.ProcessId)
      return true;
  }
  return false;
}

}  // namespace

// Creates 2 threads that acquire their designated mutex and then try to
// acquire each others' mutex to cause a deadlock.
TEST(WaitChainTest, Deadlock) {
  // 2 mutexes are needed to get a deadlock.
  ScopedHandle mutex_1 = CreateMutex(false);
  ASSERT_TRUE(mutex_1.IsValid());
  ScopedHandle mutex_2 = CreateMutex(false);
  ASSERT_TRUE(mutex_2.IsValid());

  std::unique_ptr<DeadlockThread> deadlock_thread_1 =
      CreateDeadlockThread(mutex_1.Get(), mutex_2.Get());
  std::unique_ptr<DeadlockThread> deadlock_thread_2 =
      CreateDeadlockThread(mutex_2.Get(), mutex_1.Get());

  // Signal the threads to try to acquire the other mutex.
  deadlock_thread_1->SignalToAcquireMutex();
  deadlock_thread_2->SignalToAcquireMutex();
  // Sleep to make sure the 2 threads got a chance to execute.
  Sleep(10);

  // Create a few waiting threads to get a longer wait chain.
  std::unique_ptr<SingleTaskThread> waiting_thread_1 =
      CreateJoiningThread(deadlock_thread_1.get());
  std::unique_ptr<SingleTaskThread> waiting_thread_2 =
      CreateJoiningThread(waiting_thread_1.get());

  WaitChainNodeVector wait_chain;
  bool is_deadlock;
  ASSERT_TRUE(GetThreadWaitChain(waiting_thread_2->tid(), &wait_chain,
                                 &is_deadlock, nullptr, nullptr));

  EXPECT_EQ(9U, wait_chain.size());
  EXPECT_TRUE(is_deadlock);
  EXPECT_TRUE(WaitChainStructureIsCorrect(wait_chain));
  EXPECT_FALSE(WaitChainIsCrossProcess(wait_chain));

  ASSERT_TRUE(deadlock_thread_1->Terminate());

  // The SimpleThread API expect Join() to be called before destruction.
  deadlock_thread_2->Join();
  waiting_thread_2->Join();
}

// Creates a child process that acquires a mutex and then blocks. A chain of
// threads then blocks on that mutex.
TEST(WaitChainTest, CrossProcess) {
  ScopedHandle mutex = CreateMutex(true);
  ASSERT_TRUE(mutex.IsValid());
  ScopedHandle sync_event = CreateEvent(true);
  ASSERT_TRUE(sync_event.IsValid());

  Process child_process = StartChildProcess(mutex.Get(), sync_event.Get());
  ASSERT_TRUE(child_process.IsValid());

  // Wait for the child process to signal when it's holding the mutex.
  EXPECT_EQ(WAIT_OBJECT_0, ::WaitForSingleObject(sync_event.Get(), INFINITE));

  // Create a few waiting threads to get a longer wait chain.
  std::unique_ptr<SingleTaskThread> waiting_thread_1 =
      CreateWaitingThread(mutex.Get());
  std::unique_ptr<SingleTaskThread> waiting_thread_2 =
      CreateJoiningThread(waiting_thread_1.get());
  std::unique_ptr<SingleTaskThread> waiting_thread_3 =
      CreateJoiningThread(waiting_thread_2.get());

  WaitChainNodeVector wait_chain;
  bool is_deadlock;
  ASSERT_TRUE(GetThreadWaitChain(waiting_thread_3->tid(), &wait_chain,
                                 &is_deadlock, nullptr, nullptr));

  EXPECT_EQ(7U, wait_chain.size());
  EXPECT_FALSE(is_deadlock);
  EXPECT_TRUE(WaitChainStructureIsCorrect(wait_chain));
  EXPECT_TRUE(WaitChainIsCrossProcess(wait_chain));

  // Unblock child process and wait for it to terminate.
  ASSERT_TRUE(::SetEvent(sync_event.Get()));
  ASSERT_TRUE(child_process.WaitForExit(nullptr));

  // The SimpleThread API expect Join() to be called before destruction.
  waiting_thread_3->Join();
}

}  // namespace win
}  // namespace base
