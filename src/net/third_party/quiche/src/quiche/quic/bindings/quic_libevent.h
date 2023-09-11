// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_BINDINGS_QUIC_LIBEVENT_H_
#define QUICHE_QUIC_BINDINGS_QUIC_LIBEVENT_H_

#include <memory>

#include "absl/container/node_hash_map.h"
#include "event2/event.h"
#include "event2/event_struct.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_udp_socket.h"

namespace quic {

// Provides a libevent-based implementation of QuicEventLoop.  Since libevent
// uses relative time for all timeouts, the provided clock does not need to use
// the UNIX time.
class QUICHE_EXPORT LibeventQuicEventLoop : public QuicEventLoop {
 public:
  explicit LibeventQuicEventLoop(event_base* base, QuicClock* clock);

  // QuicEventLoop implementation.
  bool SupportsEdgeTriggered() const override { return edge_triggered_; }
  std::unique_ptr<QuicAlarmFactory> CreateAlarmFactory() override {
    return std::make_unique<AlarmFactory>(this);
  }
  bool RegisterSocket(QuicUdpSocketFd fd, QuicSocketEventMask events,
                      QuicSocketEventListener* listener) override;
  bool UnregisterSocket(QuicUdpSocketFd fd) override;
  bool RearmSocket(QuicUdpSocketFd fd, QuicSocketEventMask events) override;
  bool ArtificiallyNotifyEvent(QuicUdpSocketFd fd,
                               QuicSocketEventMask events) override;
  void RunEventLoopOnce(QuicTime::Delta default_timeout) override;
  const QuicClock* GetClock() override { return clock_; }

  // Can be called from another thread to wake up the event loop from a blocking
  // RunEventLoopOnce() call.
  void WakeUp();

  event_base* base() { return base_; }
  QuicClock* clock() const { return clock_; }

 private:
  class AlarmFactory : public QuicAlarmFactory {
   public:
    AlarmFactory(LibeventQuicEventLoop* loop) : loop_(loop) {}

    // QuicAlarmFactory interface.
    QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override;
    QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
        QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
        QuicConnectionArena* arena) override;

   private:
    LibeventQuicEventLoop* loop_;
  };

  class Registration {
   public:
    Registration(LibeventQuicEventLoop* loop, QuicUdpSocketFd fd,
                 QuicSocketEventMask events, QuicSocketEventListener* listener);
    ~Registration();

    void ArtificiallyNotify(QuicSocketEventMask events);
    void Rearm(QuicSocketEventMask events);

   private:
    LibeventQuicEventLoop* loop_;
    QuicSocketEventListener* listener_;

    // Used for edge-triggered backends.
    event both_events_;
    // Used for level-triggered backends, since we may have to re-arm read
    // events and write events separately.
    event read_event_;
    event write_event_;
  };

  using RegistrationMap = absl::node_hash_map<QuicUdpSocketFd, Registration>;

  event_base* base_;
  const bool edge_triggered_;
  QuicClock* clock_;

  RegistrationMap registration_map_;
};

// RAII-style wrapper around event_base.
class QUICHE_EXPORT LibeventLoop {
 public:
  LibeventLoop(struct event_base* base) : event_base_(base) {}
  ~LibeventLoop() { event_base_free(event_base_); }

  struct event_base* event_base() { return event_base_; }

 private:
  struct event_base* event_base_;
};

// A version of LibeventQuicEventLoop that owns the supplied `event_base`.  Note
// that the inheritance order here matters, since otherwise the `event_base` in
// question will be deleted before the LibeventQuicEventLoop object referencing
// it.
class QUICHE_EXPORT LibeventQuicEventLoopWithOwnership
    : public LibeventLoop,
      public LibeventQuicEventLoop {
 public:
  static std::unique_ptr<LibeventQuicEventLoopWithOwnership> Create(
      QuicClock* clock, bool force_level_triggered = false);

  // Takes ownership of |base|.
  explicit LibeventQuicEventLoopWithOwnership(struct event_base* base,
                                              QuicClock* clock)
      : LibeventLoop(base), LibeventQuicEventLoop(base, clock) {}
};

class QUICHE_EXPORT QuicLibeventEventLoopFactory : public QuicEventLoopFactory {
 public:
  // Provides the preferred libevent backend.
  static QuicLibeventEventLoopFactory* Get() {
    static QuicLibeventEventLoopFactory* factory =
        new QuicLibeventEventLoopFactory(/*force_level_triggered=*/false);
    return factory;
  }

  // Provides the libevent backend that does not support edge-triggered
  // notifications.  Those are useful for tests, since we can test
  // level-triggered I/O even on platforms where edge-triggered is the default.
  static QuicLibeventEventLoopFactory* GetLevelTriggeredBackendForTests() {
    static QuicLibeventEventLoopFactory* factory =
        new QuicLibeventEventLoopFactory(/*force_level_triggered=*/true);
    return factory;
  }

  std::unique_ptr<QuicEventLoop> Create(QuicClock* clock) override {
    return LibeventQuicEventLoopWithOwnership::Create(clock,
                                                      force_level_triggered_);
  }
  std::string GetName() const override { return name_; }

 private:
  explicit QuicLibeventEventLoopFactory(bool force_level_triggered);

  bool force_level_triggered_;
  std::string name_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_BINDINGS_QUIC_LIBEVENT_H_
