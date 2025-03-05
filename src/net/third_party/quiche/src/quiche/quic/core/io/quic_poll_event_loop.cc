// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/io/quic_poll_event_loop.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_alarm_factory_proxy.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

namespace {

using PollMask = decltype(::pollfd().events);

PollMask GetPollMask(QuicSocketEventMask event_mask) {
  return ((event_mask & kSocketEventReadable) ? POLLIN : 0) |
         ((event_mask & kSocketEventWritable) ? POLLOUT : 0) |
         ((event_mask & kSocketEventError) ? POLLERR : 0);
}

QuicSocketEventMask GetEventMask(PollMask poll_mask) {
  return ((poll_mask & POLLIN) ? kSocketEventReadable : 0) |
         ((poll_mask & POLLOUT) ? kSocketEventWritable : 0) |
         ((poll_mask & POLLERR) ? kSocketEventError : 0);
}

}  // namespace

QuicPollEventLoop::QuicPollEventLoop(QuicClock* clock) : clock_(clock) {}

bool QuicPollEventLoop::RegisterSocket(SocketFd fd, QuicSocketEventMask events,
                                       QuicSocketEventListener* listener) {
  auto [it, success] =
      registrations_.insert({fd, std::make_shared<Registration>()});
  if (!success) {
    return false;
  }
  Registration& registration = *it->second;
  registration.events = events;
  registration.listener = listener;
  return true;
}

bool QuicPollEventLoop::UnregisterSocket(SocketFd fd) {
  return registrations_.erase(fd);
}

bool QuicPollEventLoop::RearmSocket(SocketFd fd, QuicSocketEventMask events) {
  auto it = registrations_.find(fd);
  if (it == registrations_.end()) {
    return false;
  }
  it->second->events |= events;
  return true;
}

bool QuicPollEventLoop::ArtificiallyNotifyEvent(SocketFd fd,
                                                QuicSocketEventMask events) {
  auto it = registrations_.find(fd);
  if (it == registrations_.end()) {
    return false;
  }
  has_artificial_events_pending_ = true;
  it->second->artificially_notify_at_next_iteration |= events;
  return true;
}

void QuicPollEventLoop::RunEventLoopOnce(QuicTime::Delta default_timeout) {
  const QuicTime start_time = clock_->Now();
  alarms_.ProcessAlarmsUpTo(start_time);

  QuicTime::Delta timeout = ComputePollTimeout(start_time, default_timeout);
  ProcessIoEvents(start_time, timeout);

  const QuicTime end_time = clock_->Now();
  alarms_.ProcessAlarmsUpTo(end_time);
}

QuicTime::Delta QuicPollEventLoop::ComputePollTimeout(
    QuicTime now, QuicTime::Delta default_timeout) const {
  default_timeout = std::max(default_timeout, QuicTime::Delta::Zero());
  if (has_artificial_events_pending_) {
    return QuicTime::Delta::Zero();
  }
  std::optional<QuicTime> next_alarm = alarms_.GetNextUpcomingAlarm();
  if (!next_alarm.has_value()) {
    return default_timeout;
  }
  QuicTime end_time = std::min(now + default_timeout, *next_alarm);
  if (end_time < now) {
    // We only run a single pass of processing alarm callbacks per
    // RunEventLoopOnce() call.  If an alarm schedules another alarm in the past
    // while in the callback, this will happen.
    return QuicTime::Delta::Zero();
  }
  return end_time - now;
}

int QuicPollEventLoop::PollWithRetries(absl::Span<pollfd> fds,
                                       QuicTime start_time,
                                       QuicTime::Delta timeout) {
  const QuicTime timeout_at = start_time + timeout;
  int poll_result;
  for (;;) {
    float timeout_ms = std::ceil(timeout.ToMicroseconds() / 1000.f);
    poll_result =
        PollSyscall(fds.data(), fds.size(), static_cast<int>(timeout_ms));

    // Stop if there are events or a non-EINTR error.
    bool done = poll_result > 0 || (poll_result < 0 && errno != EINTR);
    if (done) {
      break;
    }
    // Poll until `clock_` shows the timeout was exceeded.
    // PollSyscall uses a system clock internally that may run faster.
    QuicTime now = clock_->Now();
    if (now >= timeout_at) {
      break;
    }
    timeout = timeout_at - now;
  }
  return poll_result;
}

void QuicPollEventLoop::ProcessIoEvents(QuicTime start_time,
                                        QuicTime::Delta timeout) {
  // Set up the pollfd[] array.
  const size_t registration_count = registrations_.size();
  auto pollfds = std::make_unique<pollfd[]>(registration_count);
  size_t i = 0;
  for (auto& [fd, registration] : registrations_) {
    QUICHE_CHECK_LT(
        i, registration_count);  // Crash instead of out-of-bounds access.
    pollfds[i].fd = fd;
    pollfds[i].events = GetPollMask(registration->events);
    pollfds[i].revents = 0;
    ++i;
  }

  // Actually run poll(2).
  int poll_result =
      PollWithRetries(absl::Span<pollfd>(pollfds.get(), registration_count),
                      start_time, timeout);
  if (poll_result == 0 && !has_artificial_events_pending_) {
    return;
  }

  // Prepare the list of all callbacks to be called, while resetting all events,
  // since we're operating in the level-triggered mode.
  std::vector<ReadyListEntry> ready_list;
  ready_list.reserve(registration_count);
  for (i = 0; i < registration_count; i++) {
    DispatchIoEvent(ready_list, pollfds[i].fd, pollfds[i].revents);
  }
  has_artificial_events_pending_ = false;

  // Actually call all of the callbacks.
  RunReadyCallbacks(ready_list);
}

void QuicPollEventLoop::DispatchIoEvent(std::vector<ReadyListEntry>& ready_list,
                                        SocketFd fd, PollMask mask) {
  auto it = registrations_.find(fd);
  if (it == registrations_.end()) {
    QUIC_BUG(poll returned an unregistered fd) << fd;
    return;
  }
  Registration& registration = *it->second;

  mask |= GetPollMask(registration.artificially_notify_at_next_iteration);
  // poll() always returns certain classes of events even if not requested.
  mask &= GetPollMask(registration.events |
                      registration.artificially_notify_at_next_iteration);
  registration.artificially_notify_at_next_iteration = QuicSocketEventMask();
  if (!mask) {
    return;
  }

  ready_list.push_back(ReadyListEntry{fd, it->second, GetEventMask(mask)});
  registration.events &= ~GetEventMask(mask);
}

void QuicPollEventLoop::RunReadyCallbacks(
    std::vector<ReadyListEntry>& ready_list) {
  for (ReadyListEntry& entry : ready_list) {
    std::shared_ptr<Registration> registration = entry.registration.lock();
    if (!registration) {
      // The socket has been unregistered from within one of the callbacks.
      continue;
    }
    registration->listener->OnSocketEvent(this, entry.fd, entry.events);
  }
  ready_list.clear();
}

std::unique_ptr<QuicAlarmFactory> QuicPollEventLoop::CreateAlarmFactory() {
  return std::make_unique<QuicAlarmFactoryProxy>(&alarms_);
}

int QuicPollEventLoop::PollSyscall(pollfd* fds, size_t nfds, int timeout) {
#if defined(_WIN32)
  return WSAPoll(fds, nfds, timeout);
#else
  return ::poll(fds, nfds, timeout);
#endif  // defined(_WIN32)
}

}  // namespace quic
