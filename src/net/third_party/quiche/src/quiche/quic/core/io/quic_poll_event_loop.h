// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_IO_QUIC_POLL_EVENT_LOOP_H_
#define QUICHE_QUIC_CORE_IO_QUIC_POLL_EVENT_LOOP_H_

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <poll.h>
#endif

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/types/span.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_queue_alarm_factory.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/common/quiche_linked_hash_map.h"

namespace quic {

// A simple and portable implementation of QuicEventLoop using poll(2).  Works
// on all POSIX platforms (and can be potentially made to support Windows using
// WSAPoll).
//
// For most operations, this implementation has a typical runtime of
// O(N + log M), where N is the number of file descriptors, and M is the number
// of pending alarms.
//
// This API has to deal with the situations where callbacks are modified from
// the callbacks themselves.  To address this, we use the following two
// approaches:
//   1. The code does not execute any callbacks until the very end of the
//      processing, when all of the state for the event loop is consistent.
//   2. The callbacks are stored as weak pointers, since other callbacks can
//      cause them to be unregistered.
class QuicPollEventLoop : public QuicEventLoop {
 public:
  QuicPollEventLoop(QuicClock* clock);

  // QuicEventLoop implementation.
  bool SupportsEdgeTriggered() const override { return false; }
  ABSL_MUST_USE_RESULT bool RegisterSocket(
      SocketFd fd, QuicSocketEventMask events,
      QuicSocketEventListener* listener) override;
  ABSL_MUST_USE_RESULT bool UnregisterSocket(SocketFd fd) override;
  ABSL_MUST_USE_RESULT bool RearmSocket(SocketFd fd,
                                        QuicSocketEventMask events) override;
  ABSL_MUST_USE_RESULT bool ArtificiallyNotifyEvent(
      SocketFd fd, QuicSocketEventMask events) override;
  void RunEventLoopOnce(QuicTime::Delta default_timeout) override;
  std::unique_ptr<QuicAlarmFactory> CreateAlarmFactory() override;
  const QuicClock* GetClock() override { return clock_; }

 protected:
  // Allows poll(2) calls to be mocked out in unit tests.
  virtual int PollSyscall(pollfd* fds, size_t nfds, int timeout);

 private:
  friend class QuicPollEventLoopPeer;

  struct Registration {
    QuicSocketEventMask events = 0;
    QuicSocketEventListener* listener;

    QuicSocketEventMask artificially_notify_at_next_iteration = 0;
  };

  // Used for deferred execution of I/O callbacks.
  struct ReadyListEntry {
    SocketFd fd;
    std::weak_ptr<Registration> registration;
    QuicSocketEventMask events;
  };

  // We're using a linked hash map here to ensure the events are called in the
  // registration order.  This isn't strictly speaking necessary, but makes
  // testing things easier.
  using RegistrationMap =
      quiche::QuicheLinkedHashMap<SocketFd, std::shared_ptr<Registration>>;

  // Returns the timeout for the next poll(2) call.  It is typically the time at
  // which the next alarm is supposed to activate.
  QuicTime::Delta ComputePollTimeout(QuicTime now,
                                     QuicTime::Delta default_timeout) const;
  // Calls poll(2) with the provided timeout and dispatches the callbacks
  // accordingly.
  void ProcessIoEvents(QuicTime start_time, QuicTime::Delta timeout);

  // Adds the I/O callbacks for |fd| to the |ready_lits| as appropriate.
  void DispatchIoEvent(std::vector<ReadyListEntry>& ready_list, SocketFd fd,
                       short mask);  // NOLINT(runtime/int)
  // Runs all of the callbacks on the ready list.
  void RunReadyCallbacks(std::vector<ReadyListEntry>& ready_list);

  // Calls poll() while handling EINTR.  Returns the return value of poll(2)
  // system call.
  int PollWithRetries(absl::Span<pollfd> fds, QuicTime start_time,
                      QuicTime::Delta timeout);

  const QuicClock* clock_;
  RegistrationMap registrations_;
  QuicQueueAlarmFactory alarms_;
  bool has_artificial_events_pending_ = false;
};

class QuicPollEventLoopFactory : public QuicEventLoopFactory {
 public:
  static QuicPollEventLoopFactory* Get() {
    static QuicPollEventLoopFactory* factory = new QuicPollEventLoopFactory();
    return factory;
  }

  std::unique_ptr<QuicEventLoop> Create(QuicClock* clock) override {
    return std::make_unique<QuicPollEventLoop>(clock);
  }

  std::string GetName() const override { return "poll(2)"; }
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_IO_QUIC_POLL_EVENT_LOOP_H_
