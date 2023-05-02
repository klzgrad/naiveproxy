// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_IO_QUIC_EVENT_LOOP_H_
#define QUICHE_QUIC_IO_QUIC_EVENT_LOOP_H_

#include <cstdint>
#include <memory>

#include "absl/base/attributes.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_udp_socket.h"

namespace quic {

// A bitmask indicating a set of I/O events.
using QuicSocketEventMask = uint8_t;
inline constexpr QuicSocketEventMask kSocketEventReadable = 0x01;
inline constexpr QuicSocketEventMask kSocketEventWritable = 0x02;
inline constexpr QuicSocketEventMask kSocketEventError = 0x04;

class QuicEventLoop;

// A listener associated with a file descriptor.
class QUICHE_NO_EXPORT QuicSocketEventListener {
 public:
  virtual ~QuicSocketEventListener() = default;

  virtual void OnSocketEvent(QuicEventLoop* event_loop, QuicUdpSocketFd fd,
                             QuicSocketEventMask events) = 0;
};

// An abstraction for an event loop that can handle alarms and notify the
// listener about I/O events occuring to the registered UDP sockets.
//
// Note on error handling: while most of the methods below return a boolean to
// indicate whether the operation has succeeded or not, some will QUIC_BUG
// instead.
class QUICHE_NO_EXPORT QuicEventLoop {
 public:
  virtual ~QuicEventLoop() = default;

  // Indicates whether the event loop implementation supports edge-triggered
  // notifications.  If true, all of the events are permanent and are notified
  // as long as they are registered.  If false, whenever an event is triggered,
  // the event registration is unset and has to be re-armed using RearmSocket().
  virtual bool SupportsEdgeTriggered() const = 0;

  // Watches for all of the requested |events| that occur on the |fd| and
  // notifies the |listener| about them.  |fd| must not be already registered;
  // if it is, the function returns false.  The |listener| must be alive for as
  // long as it is registered.
  virtual ABSL_MUST_USE_RESULT bool RegisterSocket(
      QuicUdpSocketFd fd, QuicSocketEventMask events,
      QuicSocketEventListener* listener) = 0;
  // Removes the listener associated with |fd|.  Returns false if the listener
  // is not found.
  virtual ABSL_MUST_USE_RESULT bool UnregisterSocket(QuicUdpSocketFd fd) = 0;
  // Adds |events| to the list of the listened events for |fd|, given that |fd|
  // is already registered.  Must be only called if SupportsEdgeTriggered() is
  // false.
  virtual ABSL_MUST_USE_RESULT bool RearmSocket(QuicUdpSocketFd fd,
                                                QuicSocketEventMask events) = 0;
  // Causes the |fd| to be notified of |events| on the next event loop iteration
  // even if none of the specified events has happened.
  virtual ABSL_MUST_USE_RESULT bool ArtificiallyNotifyEvent(
      QuicUdpSocketFd fd, QuicSocketEventMask events) = 0;

  // Runs a single iteration of the event loop.  The iteration will run for at
  // most |default_timeout|.
  virtual void RunEventLoopOnce(QuicTime::Delta default_timeout) = 0;

  // Returns an alarm factory that allows alarms to be scheduled on this event
  // loop.
  virtual std::unique_ptr<QuicAlarmFactory> CreateAlarmFactory() = 0;

  // Returns the clock that is used by the alarm factory that the event loop
  // provides.
  virtual const QuicClock* GetClock() = 0;
};

// A factory object for the event loop. Every implementation is expected to have
// a static singleton instance.
class QUICHE_NO_EXPORT QuicEventLoopFactory {
 public:
  virtual ~QuicEventLoopFactory() {}

  // Creates an event loop.  Note that |clock| may be ignored if the event loop
  // implementation uses its own clock internally.
  virtual std::unique_ptr<QuicEventLoop> Create(QuicClock* clock) = 0;

  // A human-readable name of the event loop implementation used in diagnostics
  // output.
  virtual std::string GetName() const = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_IO_QUIC_EVENT_LOOP_H_
