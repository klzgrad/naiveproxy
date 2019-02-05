// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/log/net_log.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/simple_thread.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source_type.h"
#include "net/log/test_net_log.h"
#include "net/log/test_net_log_entry.h"
#include "net/log/test_net_log_util.h"

namespace net {

namespace {

const int kThreads = 10;
const int kEvents = 100;

// Under the hood a NetLogCaptureMode is simply an int. But for layering reasons
// this internal value is not exposed. These tests need to serialize a
// NetLogCaptureMode to a base::Value, so create our own private mapping.
int CaptureModeToInt(NetLogCaptureMode capture_mode) {
  if (capture_mode == NetLogCaptureMode::Default())
    return 0;
  if (capture_mode == NetLogCaptureMode::IncludeCookiesAndCredentials())
    return 1;
  if (capture_mode == NetLogCaptureMode::IncludeSocketBytes())
    return 2;

  ADD_FAILURE() << "Unknown capture mode";
  return -1;
}

std::unique_ptr<base::Value> CaptureModeToValue(
    NetLogCaptureMode capture_mode) {
  return std::make_unique<base::Value>(CaptureModeToInt(capture_mode));
}

std::unique_ptr<base::Value> NetCaptureModeCallback(
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->Set("capture_mode", CaptureModeToValue(capture_mode));
  return std::move(dict);
}

TEST(NetLogTest, Basic) {
  TestNetLog net_log;
  TestNetLogEntry::List entries;
  net_log.GetEntries(&entries);
  EXPECT_EQ(0u, entries.size());

  net_log.AddGlobalEntry(NetLogEventType::CANCELLED);

  net_log.GetEntries(&entries);
  ASSERT_EQ(1u, entries.size());
  EXPECT_EQ(NetLogEventType::CANCELLED, entries[0].type);
  EXPECT_EQ(NetLogSourceType::NONE, entries[0].source.type);
  EXPECT_NE(NetLogSource::kInvalidId, entries[0].source.id);
  EXPECT_EQ(NetLogEventPhase::NONE, entries[0].phase);
  EXPECT_GE(base::TimeTicks::Now(), entries[0].time);
  EXPECT_FALSE(entries[0].params);
}

// Check that the correct CaptureMode is sent to NetLog Value callbacks.
TEST(NetLogTest, CaptureModes) {
  NetLogCaptureMode kModes[] = {
      NetLogCaptureMode::Default(),
      NetLogCaptureMode::IncludeCookiesAndCredentials(),
      NetLogCaptureMode::IncludeSocketBytes(),
  };

  TestNetLog net_log;

  for (NetLogCaptureMode mode : kModes) {
    net_log.SetCaptureMode(mode);
    EXPECT_EQ(mode, net_log.GetObserver()->capture_mode());

    net_log.AddGlobalEntry(NetLogEventType::SOCKET_ALIVE,
                           base::Bind(NetCaptureModeCallback));

    TestNetLogEntry::List entries;
    net_log.GetEntries(&entries);

    ASSERT_EQ(1u, entries.size());
    EXPECT_EQ(NetLogEventType::SOCKET_ALIVE, entries[0].type);
    EXPECT_EQ(NetLogSourceType::NONE, entries[0].source.type);
    EXPECT_NE(NetLogSource::kInvalidId, entries[0].source.id);
    EXPECT_EQ(NetLogEventPhase::NONE, entries[0].phase);
    EXPECT_GE(base::TimeTicks::Now(), entries[0].time);

    int logged_capture_mode;
    ASSERT_TRUE(
        entries[0].GetIntegerValue("capture_mode", &logged_capture_mode));
    EXPECT_EQ(CaptureModeToInt(mode), logged_capture_mode);

    net_log.Clear();
  }
}

class CountingObserver : public NetLog::ThreadSafeObserver {
 public:
  CountingObserver() : count_(0) {}

  ~CountingObserver() override {
    if (net_log())
      net_log()->RemoveObserver(this);
  }

  void OnAddEntry(const NetLogEntry& entry) override { ++count_; }

  int count() const { return count_; }

 private:
  int count_;
};

class LoggingObserver : public NetLog::ThreadSafeObserver {
 public:
  LoggingObserver() = default;

  ~LoggingObserver() override {
    if (net_log())
      net_log()->RemoveObserver(this);
  }

  void OnAddEntry(const NetLogEntry& entry) override {
    std::unique_ptr<base::DictionaryValue> dict =
        base::DictionaryValue::From(entry.ToValue());
    ASSERT_TRUE(dict);
    values_.push_back(std::move(dict));
  }

  size_t GetNumValues() const { return values_.size(); }
  base::DictionaryValue* GetValue(size_t index) const {
    return values_[index].get();
  }

 private:
  std::vector<std::unique_ptr<base::DictionaryValue>> values_;
};

void AddEvent(NetLog* net_log) {
  net_log->AddGlobalEntry(NetLogEventType::CANCELLED,
                          base::Bind(CaptureModeToValue));
}

// A thread that waits until an event has been signalled before calling
// RunTestThread.
class NetLogTestThread : public base::SimpleThread {
 public:
  NetLogTestThread()
      : base::SimpleThread("NetLogTest"), net_log_(NULL), start_event_(NULL) {}

  // We'll wait for |start_event| to be triggered before calling a subclass's
  // subclass's RunTestThread() function.
  void Init(NetLog* net_log, base::WaitableEvent* start_event) {
    start_event_ = start_event;
    net_log_ = net_log;
  }

  void Run() override {
    start_event_->Wait();
    RunTestThread();
  }

  // Subclasses must override this with the code they want to run on their
  // thread.
  virtual void RunTestThread() = 0;

 protected:
  NetLog* net_log_;

 private:
  // Only triggered once all threads have been created, to make it less likely
  // each thread completes before the next one starts.
  base::WaitableEvent* start_event_;

  DISALLOW_COPY_AND_ASSIGN(NetLogTestThread);
};

// A thread that adds a bunch of events to the NetLog.
class AddEventsTestThread : public NetLogTestThread {
 public:
  AddEventsTestThread() = default;
  ~AddEventsTestThread() override = default;

 private:
  void RunTestThread() override {
    for (int i = 0; i < kEvents; ++i)
      AddEvent(net_log_);
  }

  DISALLOW_COPY_AND_ASSIGN(AddEventsTestThread);
};

// A thread that adds and removes an observer from the NetLog repeatedly.
class AddRemoveObserverTestThread : public NetLogTestThread {
 public:
  AddRemoveObserverTestThread() = default;

  ~AddRemoveObserverTestThread() override { EXPECT_TRUE(!observer_.net_log()); }

 private:
  void RunTestThread() override {
    for (int i = 0; i < kEvents; ++i) {
      ASSERT_FALSE(observer_.net_log());

      net_log_->AddObserver(&observer_,
                            NetLogCaptureMode::IncludeCookiesAndCredentials());
      ASSERT_EQ(net_log_, observer_.net_log());
      ASSERT_EQ(NetLogCaptureMode::IncludeCookiesAndCredentials(),
                observer_.capture_mode());

      net_log_->SetObserverCaptureMode(&observer_,
                                       NetLogCaptureMode::IncludeSocketBytes());
      ASSERT_EQ(net_log_, observer_.net_log());
      ASSERT_EQ(NetLogCaptureMode::IncludeSocketBytes(),
                observer_.capture_mode());

      net_log_->RemoveObserver(&observer_);
      ASSERT_TRUE(!observer_.net_log());
    }
  }

  CountingObserver observer_;

  DISALLOW_COPY_AND_ASSIGN(AddRemoveObserverTestThread);
};

// Creates |kThreads| threads of type |ThreadType| and then runs them all
// to completion.
template <class ThreadType>
void RunTestThreads(NetLog* net_log) {
  ThreadType threads[kThreads];
  base::WaitableEvent start_event(
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED);

  for (size_t i = 0; i < arraysize(threads); ++i) {
    threads[i].Init(net_log, &start_event);
    threads[i].Start();
  }

  start_event.Signal();

  for (size_t i = 0; i < arraysize(threads); ++i)
    threads[i].Join();
}

// Makes sure that events on multiple threads are dispatched to all observers.
TEST(NetLogTest, NetLogEventThreads) {
  NetLog net_log;

  // Attach some observers.  Since they're created after |net_log|, they'll
  // safely detach themselves on destruction.
  CountingObserver observers[3];
  for (size_t i = 0; i < arraysize(observers); ++i) {
    net_log.AddObserver(&observers[i], NetLogCaptureMode::IncludeSocketBytes());
  }

  // Run a bunch of threads to completion, each of which will emit events to
  // |net_log|.
  RunTestThreads<AddEventsTestThread>(&net_log);

  // Check that each observer saw the emitted events.
  const int kTotalEvents = kThreads * kEvents;
  for (size_t i = 0; i < arraysize(observers); ++i)
    EXPECT_EQ(kTotalEvents, observers[i].count());
}

// Test adding and removing a single observer.
TEST(NetLogTest, NetLogAddRemoveObserver) {
  NetLog net_log;
  CountingObserver observer;

  AddEvent(&net_log);
  EXPECT_EQ(0, observer.count());
  EXPECT_EQ(NULL, observer.net_log());
  EXPECT_FALSE(net_log.IsCapturing());

  // Add the observer and add an event.
  net_log.AddObserver(&observer,
                      NetLogCaptureMode::IncludeCookiesAndCredentials());
  EXPECT_TRUE(net_log.IsCapturing());
  EXPECT_EQ(&net_log, observer.net_log());
  EXPECT_EQ(NetLogCaptureMode::IncludeCookiesAndCredentials(),
            observer.capture_mode());
  EXPECT_TRUE(net_log.IsCapturing());

  AddEvent(&net_log);
  EXPECT_EQ(1, observer.count());

  // Change the observer's logging level and add an event.
  net_log.SetObserverCaptureMode(&observer,
                                 NetLogCaptureMode::IncludeSocketBytes());
  EXPECT_EQ(&net_log, observer.net_log());
  EXPECT_EQ(NetLogCaptureMode::IncludeSocketBytes(), observer.capture_mode());
  EXPECT_TRUE(net_log.IsCapturing());

  AddEvent(&net_log);
  EXPECT_EQ(2, observer.count());

  // Remove observer and add an event.
  net_log.RemoveObserver(&observer);
  EXPECT_EQ(NULL, observer.net_log());
  EXPECT_FALSE(net_log.IsCapturing());

  AddEvent(&net_log);
  EXPECT_EQ(2, observer.count());

  // Add the observer a final time, and add an event.
  net_log.AddObserver(&observer, NetLogCaptureMode::IncludeSocketBytes());
  EXPECT_EQ(&net_log, observer.net_log());
  EXPECT_EQ(NetLogCaptureMode::IncludeSocketBytes(), observer.capture_mode());
  EXPECT_TRUE(net_log.IsCapturing());

  AddEvent(&net_log);
  EXPECT_EQ(3, observer.count());
}

// Test adding and removing two observers at different log levels.
TEST(NetLogTest, NetLogTwoObservers) {
  NetLog net_log;
  LoggingObserver observer[2];

  // Add first observer.
  net_log.AddObserver(&observer[0],
                      NetLogCaptureMode::IncludeCookiesAndCredentials());
  EXPECT_EQ(&net_log, observer[0].net_log());
  EXPECT_EQ(NULL, observer[1].net_log());
  EXPECT_EQ(NetLogCaptureMode::IncludeCookiesAndCredentials(),
            observer[0].capture_mode());
  EXPECT_TRUE(net_log.IsCapturing());

  // Add second observer observer.
  net_log.AddObserver(&observer[1], NetLogCaptureMode::IncludeSocketBytes());
  EXPECT_EQ(&net_log, observer[0].net_log());
  EXPECT_EQ(&net_log, observer[1].net_log());
  EXPECT_EQ(NetLogCaptureMode::IncludeCookiesAndCredentials(),
            observer[0].capture_mode());
  EXPECT_EQ(NetLogCaptureMode::IncludeSocketBytes(),
            observer[1].capture_mode());
  EXPECT_TRUE(net_log.IsCapturing());

  // Add event and make sure both observers receive it at their respective log
  // levels.
  int param;
  AddEvent(&net_log);
  ASSERT_EQ(1U, observer[0].GetNumValues());
  ASSERT_TRUE(observer[0].GetValue(0)->GetInteger("params", &param));
  EXPECT_EQ(CaptureModeToInt(observer[0].capture_mode()), param);
  ASSERT_EQ(1U, observer[1].GetNumValues());
  ASSERT_TRUE(observer[1].GetValue(0)->GetInteger("params", &param));
  EXPECT_EQ(CaptureModeToInt(observer[1].capture_mode()), param);

  // Remove second observer.
  net_log.RemoveObserver(&observer[1]);
  EXPECT_EQ(&net_log, observer[0].net_log());
  EXPECT_EQ(NULL, observer[1].net_log());
  EXPECT_EQ(NetLogCaptureMode::IncludeCookiesAndCredentials(),
            observer[0].capture_mode());
  EXPECT_TRUE(net_log.IsCapturing());

  // Add event and make sure only second observer gets it.
  AddEvent(&net_log);
  EXPECT_EQ(2U, observer[0].GetNumValues());
  EXPECT_EQ(1U, observer[1].GetNumValues());

  // Remove first observer.
  net_log.RemoveObserver(&observer[0]);
  EXPECT_EQ(NULL, observer[0].net_log());
  EXPECT_EQ(NULL, observer[1].net_log());
  EXPECT_FALSE(net_log.IsCapturing());

  // Add event and make sure neither observer gets it.
  AddEvent(&net_log);
  EXPECT_EQ(2U, observer[0].GetNumValues());
  EXPECT_EQ(1U, observer[1].GetNumValues());
}

// Makes sure that adding and removing observers simultaneously on different
// threads works.
TEST(NetLogTest, NetLogAddRemoveObserverThreads) {
  NetLog net_log;

  // Run a bunch of threads to completion, each of which will repeatedly add
  // and remove an observer, and set its logging level.
  RunTestThreads<AddRemoveObserverTestThread>(&net_log);
}

}  // namespace

}  // namespace net
