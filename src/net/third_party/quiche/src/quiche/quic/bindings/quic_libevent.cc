// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/bindings/quic_libevent.h"

#include <memory>
#include <utility>

#include "absl/time/time.h"
#include "event2/event.h"
#include "event2/event_struct.h"
#include "event2/thread.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"

namespace quic {

using LibeventEventMask = short;  // NOLINT(runtime/int)

QuicSocketEventMask LibeventEventMaskToQuicEvents(int events) {
  return ((events & EV_READ) ? kSocketEventReadable : 0) |
         ((events & EV_WRITE) ? kSocketEventWritable : 0);
}

LibeventEventMask QuicEventsToLibeventEventMask(QuicSocketEventMask events) {
  return ((events & kSocketEventReadable) ? EV_READ : 0) |
         ((events & kSocketEventWritable) ? EV_WRITE : 0);
}

class LibeventAlarm : public QuicAlarm {
 public:
  LibeventAlarm(LibeventQuicEventLoop* loop,
                QuicArenaScopedPtr<QuicAlarm::Delegate> delegate)
      : QuicAlarm(std::move(delegate)), clock_(loop->clock()) {
    event_.reset(evtimer_new(
        loop->base(),
        [](evutil_socket_t, LibeventEventMask, void* arg) {
          LibeventAlarm* self = reinterpret_cast<LibeventAlarm*>(arg);
          self->Fire();
        },
        this));
  }

 protected:
  void SetImpl() override {
    absl::Duration timeout =
        absl::Microseconds((deadline() - clock_->Now()).ToMicroseconds());
    timeval unix_time = absl::ToTimeval(timeout);
    event_add(event_.get(), &unix_time);
  }

  void CancelImpl() override { event_del(event_.get()); }

 private:
  // While we inline `struct event` elsewhere, it is actually quite large, so
  // doing that for the libevent-based QuicAlarm would cause it to not fit into
  // the QuicConnectionArena.
  struct EventDeleter {
    void operator()(event* ev) { event_free(ev); }
  };
  std::unique_ptr<event, EventDeleter> event_;
  QuicClock* clock_;
};

LibeventQuicEventLoop::LibeventQuicEventLoop(event_base* base, QuicClock* clock)
    : base_(base),
      edge_triggered_(event_base_get_features(base) & EV_FEATURE_ET),
      clock_(clock) {
  QUICHE_CHECK_LE(sizeof(event), event_get_struct_event_size())
      << "libevent ABI mismatch: sizeof(event) is bigger than the one QUICHE "
         "has been compiled with";
}

bool LibeventQuicEventLoop::RegisterSocket(QuicUdpSocketFd fd,
                                           QuicSocketEventMask events,
                                           QuicSocketEventListener* listener) {
  auto [it, success] =
      registration_map_.try_emplace(fd, this, fd, events, listener);
  return success;
}

bool LibeventQuicEventLoop::UnregisterSocket(QuicUdpSocketFd fd) {
  return registration_map_.erase(fd);
}

bool LibeventQuicEventLoop::RearmSocket(QuicUdpSocketFd fd,
                                        QuicSocketEventMask events) {
  if (edge_triggered_) {
    QUICHE_BUG(LibeventQuicEventLoop_RearmSocket_called_on_ET)
        << "RearmSocket() called on an edge-triggered event loop";
    return false;
  }
  auto it = registration_map_.find(fd);
  if (it == registration_map_.end()) {
    return false;
  }
  it->second.Rearm(events);
  return true;
}

bool LibeventQuicEventLoop::ArtificiallyNotifyEvent(
    QuicUdpSocketFd fd, QuicSocketEventMask events) {
  auto it = registration_map_.find(fd);
  if (it == registration_map_.end()) {
    return false;
  }
  it->second.ArtificiallyNotify(events);
  return true;
}

void LibeventQuicEventLoop::RunEventLoopOnce(QuicTime::Delta default_timeout) {
  timeval timeout =
      absl::ToTimeval(absl::Microseconds(default_timeout.ToMicroseconds()));
  event_base_loopexit(base_, &timeout);
  event_base_loop(base_, EVLOOP_ONCE);
}

void LibeventQuicEventLoop::WakeUp() {
  timeval timeout = absl::ToTimeval(absl::ZeroDuration());
  event_base_loopexit(base_, &timeout);
}

LibeventQuicEventLoop::Registration::Registration(
    LibeventQuicEventLoop* loop, QuicUdpSocketFd fd, QuicSocketEventMask events,
    QuicSocketEventListener* listener)
    : loop_(loop), listener_(listener) {
  event_callback_fn callback = [](evutil_socket_t fd, LibeventEventMask events,
                                  void* arg) {
    auto* self = reinterpret_cast<LibeventQuicEventLoop::Registration*>(arg);
    self->listener_->OnSocketEvent(self->loop_, fd,
                                   LibeventEventMaskToQuicEvents(events));
  };

  if (loop_->SupportsEdgeTriggered()) {
    LibeventEventMask mask =
        QuicEventsToLibeventEventMask(events) | EV_PERSIST | EV_ET;
    event_assign(&both_events_, loop_->base(), fd, mask, callback, this);
    event_add(&both_events_, nullptr);
  } else {
    event_assign(&read_event_, loop_->base(), fd, EV_READ, callback, this);
    event_assign(&write_event_, loop_->base(), fd, EV_WRITE, callback, this);
    Rearm(events);
  }
}

LibeventQuicEventLoop::Registration::~Registration() {
  if (loop_->SupportsEdgeTriggered()) {
    event_del(&both_events_);
  } else {
    event_del(&read_event_);
    event_del(&write_event_);
  }
}

void LibeventQuicEventLoop::Registration::ArtificiallyNotify(
    QuicSocketEventMask events) {
  if (loop_->SupportsEdgeTriggered()) {
    event_active(&both_events_, QuicEventsToLibeventEventMask(events), 0);
    return;
  }

  if (events & kSocketEventReadable) {
    event_active(&read_event_, EV_READ, 0);
  }
  if (events & kSocketEventWritable) {
    event_active(&write_event_, EV_WRITE, 0);
  }
}

void LibeventQuicEventLoop::Registration::Rearm(QuicSocketEventMask events) {
  QUICHE_DCHECK(!loop_->SupportsEdgeTriggered());
  if (events & kSocketEventReadable) {
    event_add(&read_event_, nullptr);
  }
  if (events & kSocketEventWritable) {
    event_add(&write_event_, nullptr);
  }
}

QuicAlarm* LibeventQuicEventLoop::AlarmFactory::CreateAlarm(
    QuicAlarm::Delegate* delegate) {
  return new LibeventAlarm(loop_,
                           QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate));
}

QuicArenaScopedPtr<QuicAlarm> LibeventQuicEventLoop::AlarmFactory::CreateAlarm(
    QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
    QuicConnectionArena* arena) {
  if (arena != nullptr) {
    return arena->New<LibeventAlarm>(loop_, std::move(delegate));
  }
  return QuicArenaScopedPtr<QuicAlarm>(
      new LibeventAlarm(loop_, std::move(delegate)));
}

QuicLibeventEventLoopFactory::QuicLibeventEventLoopFactory(
    bool force_level_triggered)
    : force_level_triggered_(force_level_triggered) {
  std::unique_ptr<QuicEventLoop> event_loop = Create(QuicDefaultClock::Get());
  name_ = absl::StrFormat(
      "libevent(%s)",
      event_base_get_method(
          static_cast<LibeventQuicEventLoopWithOwnership*>(event_loop.get())
              ->base()));
}

struct LibeventConfigDeleter {
  void operator()(event_config* config) { event_config_free(config); }
};

std::unique_ptr<LibeventQuicEventLoopWithOwnership>
LibeventQuicEventLoopWithOwnership::Create(QuicClock* clock,
                                           bool force_level_triggered) {
  // Required for event_base_loopbreak() to actually work.
  static int threads_initialized = []() {
#ifdef _WIN32
    return evthread_use_windows_threads();
#else
    return evthread_use_pthreads();
#endif
  }();
  QUICHE_DCHECK_EQ(threads_initialized, 0);

  std::unique_ptr<event_config, LibeventConfigDeleter> config(
      event_config_new());
  if (force_level_triggered) {
    // epoll and kqueue are the two only current libevent backends that support
    // edge-triggered I/O.
    event_config_avoid_method(config.get(), "epoll");
    event_config_avoid_method(config.get(), "kqueue");
  }
  return std::make_unique<LibeventQuicEventLoopWithOwnership>(
      event_base_new_with_config(config.get()), clock);
}

}  // namespace quic
