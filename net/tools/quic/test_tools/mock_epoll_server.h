// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_TEST_TOOLS_MOCK_EPOLL_SERVER_H_
#define NET_TOOLS_QUIC_TEST_TOOLS_MOCK_EPOLL_SERVER_H_

#include <stddef.h>
#include <stdint.h>

#include <unordered_map>
#include <unordered_set>

#include "base/logging.h"
#include "base/macros.h"
#include "net/tools/epoll_server/epoll_server.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace net {
namespace test {

// Unlike the full MockEpollServer, this only lies about the time but lets
// fd events operate normally.  Usefully when interacting with real backends
// but wanting to skip forward in time to trigger timeouts.
class FakeTimeEpollServer : public EpollServer {
 public:
  FakeTimeEpollServer();
  ~FakeTimeEpollServer() override;

  // Replaces the EpollServer NowInUsec.
  int64_t NowInUsec() const override;

  void set_now_in_usec(int64_t nius) { now_in_usec_ = nius; }

  // Advances the virtual 'now' by advancement_usec.
  void AdvanceBy(int64_t advancement_usec) {
    set_now_in_usec(NowInUsec() + advancement_usec);
  }

  // Advances the virtual 'now' by advancement_usec, and
  // calls WaitForEventAndExecteCallbacks.
  // Note that the WaitForEventsAndExecuteCallbacks invocation
  // may cause NowInUs to advance beyond what was specified here.
  // If that is not desired, use the AdvanceByExactly calls.
  void AdvanceByAndWaitForEventsAndExecuteCallbacks(int64_t advancement_usec) {
    AdvanceBy(advancement_usec);
    WaitForEventsAndExecuteCallbacks();
  }

 private:
  int64_t now_in_usec_;

  DISALLOW_COPY_AND_ASSIGN(FakeTimeEpollServer);
};

class MockEpollServer : public FakeTimeEpollServer {
 public:  // type definitions
  using EventQueue = std::unordered_multimap<int64_t, struct epoll_event>;

  MockEpollServer();
  ~MockEpollServer() override;

  // time_in_usec is the time at which the event specified
  // by 'ee' will be delivered. Note that it -is- possible
  // to add an event for a time which has already been passed..
  // .. upon the next time that the callbacks are invoked,
  // all events which are in the 'past' will be delivered.
  void AddEvent(int64_t time_in_usec, const struct epoll_event& ee) {
    event_queue_.insert(std::make_pair(time_in_usec, ee));
  }

  // Advances the virtual 'now' by advancement_usec,
  // and ensure that the next invocation of
  // WaitForEventsAndExecuteCallbacks goes no farther than
  // advancement_usec from the current time.
  void AdvanceByExactly(int64_t advancement_usec) {
    until_in_usec_ = NowInUsec() + advancement_usec;
    set_now_in_usec(NowInUsec() + advancement_usec);
  }

  // As above, except calls WaitForEventsAndExecuteCallbacks.
  void AdvanceByExactlyAndCallCallbacks(int64_t advancement_usec) {
    AdvanceByExactly(advancement_usec);
    WaitForEventsAndExecuteCallbacks();
  }

  std::unordered_set<AlarmCB*>::size_type NumberOfAlarms() const {
    return all_alarms_.size();
  }

 protected:  // functions
  // These functions do nothing here, as we're not actually
  // using the epoll_* syscalls.
  void DelFD(int fd) const override {}
  void AddFD(int fd, int event_mask) const override {}
  void ModFD(int fd, int event_mask) const override {}

  // Replaces the epoll_server's epoll_wait_impl.
  int epoll_wait_impl(int epfd,
                      struct epoll_event* events,
                      int max_events,
                      int timeout_in_ms) override;
  void SetNonblocking(int fd) override {}

 private:  // members
  EventQueue event_queue_;
  int64_t until_in_usec_;

  DISALLOW_COPY_AND_ASSIGN(MockEpollServer);
};

}  // namespace test
}  // namespace net

#endif  // NET_TOOLS_QUIC_TEST_TOOLS_MOCK_EPOLL_SERVER_H_
