// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/epoll_server/fake_simple_epoll_server.h"

namespace epoll_server {
namespace test {

FakeTimeSimpleEpollServer::FakeTimeSimpleEpollServer() : now_in_usec_(0) {}

FakeTimeSimpleEpollServer::~FakeTimeSimpleEpollServer() = default;

int64_t FakeTimeSimpleEpollServer::NowInUsec() const { return now_in_usec_; }

FakeSimpleEpollServer::FakeSimpleEpollServer() : until_in_usec_(-1) {}

FakeSimpleEpollServer::~FakeSimpleEpollServer() = default;

int FakeSimpleEpollServer::epoll_wait_impl(int epfd, struct epoll_event* events,
                                           int max_events, int timeout_in_ms) {
  int num_events = 0;
  while (!event_queue_.empty() && num_events < max_events &&
         event_queue_.begin()->first <= NowInUsec() &&
         ((until_in_usec_ == -1) ||
          (event_queue_.begin()->first < until_in_usec_))) {
    int64_t event_time_in_usec = event_queue_.begin()->first;
    events[num_events] = event_queue_.begin()->second;
    if (event_time_in_usec > NowInUsec()) {
      set_now_in_usec(event_time_in_usec);
    }
    event_queue_.erase(event_queue_.begin());
    ++num_events;
  }
  if (num_events == 0) {       // then we'd have waited 'till the timeout.
    if (until_in_usec_ < 0) {  // then we don't care what the final time is.
      if (timeout_in_ms > 0) {
        AdvanceBy(timeout_in_ms * 1000);
      }
    } else {  // except we assume that we don't wait for the timeout
      // period if until_in_usec_ is a positive number.
      set_now_in_usec(until_in_usec_);
      // And reset until_in_usec_ to signal no waiting (as
      // the AdvanceByExactly* stuff is meant to be one-shot,
      // as are all similar net::EpollServer functions)
      until_in_usec_ = -1;
    }
  }
  if (until_in_usec_ >= 0) {
    CHECK(until_in_usec_ >= NowInUsec());
  }
  return num_events;
}

}  // namespace test
}  // namespace epoll_server
