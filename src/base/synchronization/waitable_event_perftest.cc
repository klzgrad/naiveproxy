// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/synchronization/waitable_event.h"

#include <numeric>

#include "base/threading/simple_thread.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/perf/perf_test.h"

namespace base {

namespace {

class TraceWaitableEvent {
 public:
  TraceWaitableEvent(size_t samples)
      : event_(WaitableEvent::ResetPolicy::AUTOMATIC,
               WaitableEvent::InitialState::NOT_SIGNALED),
        samples_(samples) {
    signal_times_.reserve(samples);
    wait_times_.reserve(samples);
  }

  ~TraceWaitableEvent() = default;

  void Signal() {
    TimeTicks start = TimeTicks::Now();
    event_.Signal();
    signal_times_.push_back(TimeTicks::Now() - start);
  }

  void Wait() {
    TimeTicks start = TimeTicks::Now();
    event_.Wait();
    wait_times_.push_back(TimeTicks::Now() - start);
  }

  bool TimedWaitUntil(const TimeTicks& end_time) {
    TimeTicks start = TimeTicks::Now();
    bool signaled = event_.TimedWaitUntil(end_time);
    wait_times_.push_back(TimeTicks::Now() - start);
    return signaled;
  }

  bool IsSignaled() { return event_.IsSignaled(); }

  const std::vector<TimeDelta>& signal_times() const { return signal_times_; }
  const std::vector<TimeDelta>& wait_times() const { return wait_times_; }
  size_t samples() const { return samples_; }

 private:
  WaitableEvent event_;

  std::vector<TimeDelta> signal_times_;
  std::vector<TimeDelta> wait_times_;

  const size_t samples_;

  DISALLOW_COPY_AND_ASSIGN(TraceWaitableEvent);
};

class SignalerThread : public SimpleThread {
 public:
  SignalerThread(TraceWaitableEvent* waiter, TraceWaitableEvent* signaler)
      : SimpleThread("WaitableEventPerfTest signaler"),
        waiter_(waiter),
        signaler_(signaler) {}

  ~SignalerThread() override = default;

  void Run() override {
    while (!stop_event_.IsSignaled()) {
      if (waiter_)
        waiter_->Wait();
      if (signaler_)
        signaler_->Signal();
    }
  }

  // Signals the thread to stop on the next iteration of its loop (which
  // will happen immediately if no |waiter_| is present or is signaled.
  void RequestStop() { stop_event_.Signal(); }

 private:
  WaitableEvent stop_event_{WaitableEvent::ResetPolicy::MANUAL,
                            WaitableEvent::InitialState::NOT_SIGNALED};
  TraceWaitableEvent* waiter_;
  TraceWaitableEvent* signaler_;
  DISALLOW_COPY_AND_ASSIGN(SignalerThread);
};

void PrintPerfWaitableEvent(const TraceWaitableEvent* event,
                            const std::string& modifier,
                            const std::string& trace) {
  TimeDelta signal_time = std::accumulate(
      event->signal_times().begin(), event->signal_times().end(), TimeDelta());
  TimeDelta wait_time = std::accumulate(event->wait_times().begin(),
                                        event->wait_times().end(), TimeDelta());
  perf_test::PrintResult(
      "signal_time", modifier, trace,
      static_cast<size_t>(signal_time.InNanoseconds()) / event->samples(),
      "ns/sample", true);
  perf_test::PrintResult(
      "wait_time", modifier, trace,
      static_cast<size_t>(wait_time.InNanoseconds()) / event->samples(),
      "ns/sample", true);
}

}  // namespace

TEST(WaitableEventPerfTest, SingleThread) {
  const size_t kSamples = 1000;

  TraceWaitableEvent event(kSamples);

  for (size_t i = 0; i < kSamples; ++i) {
    event.Signal();
    event.Wait();
  }

  PrintPerfWaitableEvent(&event, "", "singlethread-1000-samples");
}

TEST(WaitableEventPerfTest, MultipleThreads) {
  const size_t kSamples = 1000;

  TraceWaitableEvent waiter(kSamples);
  TraceWaitableEvent signaler(kSamples);

  // The other thread will wait and signal on the respective opposite events.
  SignalerThread thread(&signaler, &waiter);
  thread.Start();

  for (size_t i = 0; i < kSamples; ++i) {
    signaler.Signal();
    waiter.Wait();
  }

  // Signal the stop event and then make sure the signaler event it is
  // waiting on is also signaled.
  thread.RequestStop();
  signaler.Signal();

  thread.Join();

  PrintPerfWaitableEvent(&waiter, "_waiter", "multithread-1000-samples");
  PrintPerfWaitableEvent(&signaler, "_signaler", "multithread-1000-samples");
}

TEST(WaitableEventPerfTest, Throughput) {
  // Reserve a lot of sample space.
  const size_t kCapacity = 500000;
  TraceWaitableEvent event(kCapacity);

  SignalerThread thread(nullptr, &event);
  thread.Start();

  TimeTicks end_time = TimeTicks::Now() + TimeDelta::FromSeconds(1);
  size_t count = 0;
  while (event.TimedWaitUntil(end_time)) {
    ++count;
  }

  thread.RequestStop();
  thread.Join();

  perf_test::PrintResult("counts", "", "throughput", count, "signals", true);
  PrintPerfWaitableEvent(&event, "", "throughput");

  // Make sure that allocation didn't happen during the test.
  EXPECT_LE(event.signal_times().capacity(), kCapacity);
  EXPECT_LE(event.wait_times().capacity(), kCapacity);
}

}  // namespace base
