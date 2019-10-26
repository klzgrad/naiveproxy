// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_epoll_alarm_factory.h"

#include <type_traits>

#include "net/third_party/quiche/src/quic/core/quic_arena_scoped_ptr.h"

namespace quic {
namespace {

class QuicEpollAlarm : public QuicAlarm {
 public:
  QuicEpollAlarm(QuicEpollServer* epoll_server,
                 QuicArenaScopedPtr<QuicAlarm::Delegate> delegate)
      : QuicAlarm(std::move(delegate)),
        epoll_server_(epoll_server),
        epoll_alarm_impl_(this) {}

 protected:
  void SetImpl() override {
    DCHECK(deadline().IsInitialized());
    epoll_server_->RegisterAlarm(
        (deadline() - QuicTime::Zero()).ToMicroseconds(), &epoll_alarm_impl_);
  }

  void CancelImpl() override {
    DCHECK(!deadline().IsInitialized());
    epoll_alarm_impl_.UnregisterIfRegistered();
  }

  void UpdateImpl() override {
    DCHECK(deadline().IsInitialized());
    int64_t epoll_deadline = (deadline() - QuicTime::Zero()).ToMicroseconds();
    if (epoll_alarm_impl_.registered()) {
      epoll_alarm_impl_.ReregisterAlarm(epoll_deadline);
    } else {
      epoll_server_->RegisterAlarm(epoll_deadline, &epoll_alarm_impl_);
    }
  }

 private:
  class EpollAlarmImpl : public QuicEpollAlarmBase {
   public:
    using int64_epoll = decltype(QuicEpollAlarmBase().OnAlarm());

    explicit EpollAlarmImpl(QuicEpollAlarm* alarm) : alarm_(alarm) {}

    // Use the same integer type as the base class.
    int64_epoll OnAlarm() override {
      QuicEpollAlarmBase::OnAlarm();
      alarm_->Fire();
      // Fire will take care of registering the alarm, if needed.
      return 0;
    }

   private:
    QuicEpollAlarm* alarm_;
  };

  QuicEpollServer* epoll_server_;
  EpollAlarmImpl epoll_alarm_impl_;
};

}  // namespace

QuicEpollAlarmFactory::QuicEpollAlarmFactory(QuicEpollServer* epoll_server)
    : epoll_server_(epoll_server) {}

QuicEpollAlarmFactory::~QuicEpollAlarmFactory() = default;

QuicAlarm* QuicEpollAlarmFactory::CreateAlarm(QuicAlarm::Delegate* delegate) {
  return new QuicEpollAlarm(epoll_server_,
                            QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate));
}

QuicArenaScopedPtr<QuicAlarm> QuicEpollAlarmFactory::CreateAlarm(
    QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
    QuicConnectionArena* arena) {
  if (arena != nullptr) {
    return arena->New<QuicEpollAlarm>(epoll_server_, std::move(delegate));
  }
  return QuicArenaScopedPtr<QuicAlarm>(
      new QuicEpollAlarm(epoll_server_, std::move(delegate)));
}

}  // namespace quic
