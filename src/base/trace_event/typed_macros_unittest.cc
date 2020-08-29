// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/typed_macros.h"

#include "base/synchronization/waitable_event.h"
#include "base/trace_event/trace_log.h"
#include "base/trace_event/typed_macros_embedder_support.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/perfetto/include/perfetto/protozero/scattered_heap_buffer.h"

#include "third_party/perfetto/protos/perfetto/trace/track_event/log_message.pbzero.h"

namespace base {
namespace trace_event {

namespace {

constexpr const char kRecordAllCategoryFilter[] = "*";

void CancelTraceAsync(WaitableEvent* flush_complete_event) {
  TraceLog::GetInstance()->CancelTracing(base::BindRepeating(
      [](WaitableEvent* complete_event,
         const scoped_refptr<base::RefCountedString>&, bool has_more_events) {
        if (!has_more_events)
          complete_event->Signal();
      },
      base::Unretained(flush_complete_event)));
}

void CancelTrace() {
  WaitableEvent flush_complete_event(WaitableEvent::ResetPolicy::AUTOMATIC,
                                     WaitableEvent::InitialState::NOT_SIGNALED);
  CancelTraceAsync(&flush_complete_event);
  flush_complete_event.Wait();
}

struct TestTrackEvent;
TestTrackEvent* g_test_track_event;

struct TestTrackEvent : public TrackEventHandle::CompletionListener {
 public:
  TestTrackEvent() {
    CHECK_EQ(g_test_track_event, nullptr)
        << "Another instance of TestTrackEvent is already active";
    g_test_track_event = this;
  }

  ~TestTrackEvent() override { g_test_track_event = nullptr; }

  void OnTrackEventCompleted() override {
    EXPECT_FALSE(event_completed);
    event_completed = true;
  }

  protozero::HeapBuffered<perfetto::protos::pbzero::TrackEvent> event;
  bool prepare_called = false;
  bool event_completed = false;
};

TrackEventHandle PrepareTrackEvent(TraceEvent*) {
  CHECK_NE(g_test_track_event, nullptr) << "TestTrackEvent not set yet";
  g_test_track_event->prepare_called = true;
  return TrackEventHandle(g_test_track_event->event.get(), g_test_track_event);
}

class TypedTraceEventTest : public testing::Test {
 public:
  TypedTraceEventTest() { EnableTypedTraceEvents(&PrepareTrackEvent); }

  ~TypedTraceEventTest() override { ResetTypedTraceEventsForTesting(); }

 protected:
  TestTrackEvent event_;
};

}  // namespace

TEST_F(TypedTraceEventTest, CallbackExecutedWhenTracingEnabled) {
  TraceLog::GetInstance()->SetEnabled(TraceConfig(kRecordAllCategoryFilter, ""),
                                      TraceLog::RECORDING_MODE);

  TRACE_EVENT("cat", "Name", [this](perfetto::EventContext ctx) {
    EXPECT_EQ(ctx.event(), event_.event.get());
    perfetto::protos::pbzero::LogMessage* log = ctx.event()->set_log_message();
    log->set_body_iid(1);
  });

  EXPECT_TRUE(event_.prepare_called);
  EXPECT_FALSE(event_.event.empty());
  EXPECT_TRUE(event_.event_completed);

  CancelTrace();
}

TEST_F(TypedTraceEventTest, CallbackNotExecutedWhenTracingDisabled) {
  TRACE_EVENT("cat", "Name", [this](perfetto::EventContext ctx) {
    EXPECT_EQ(ctx.event(), event_.event.get());
    perfetto::protos::pbzero::LogMessage* log = ctx.event()->set_log_message();
    log->set_body_iid(1);
  });

  EXPECT_FALSE(event_.prepare_called);
  EXPECT_TRUE(event_.event.empty());
  EXPECT_FALSE(event_.event_completed);
}

}  // namespace trace_event
}  // namespace base
