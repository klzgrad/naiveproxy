// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_epoll_alarm_factory.h"

#include "net/tools/epoll_server/epoll_server.h"

namespace net {

namespace {

class QuicEpollAlarm : public QuicAlarm {
 public:
  QuicEpollAlarm(EpollServer* epoll_server,
                 QuicArenaScopedPtr<Delegate> delegate)
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

 private:
  class EpollAlarmImpl : public EpollAlarm {
   public:
    explicit EpollAlarmImpl(QuicEpollAlarm* alarm) : alarm_(alarm) {}

    int64_t OnAlarm() override {
      EpollAlarm::OnAlarm();
      alarm_->Fire();
      // Fire will take care of registering the alarm, if needed.
      return 0;
    }

   private:
    QuicEpollAlarm* alarm_;
  };

  EpollServer* epoll_server_;
  EpollAlarmImpl epoll_alarm_impl_;
};

}  // namespace

QuicEpollAlarmFactory::QuicEpollAlarmFactory(EpollServer* epoll_server)
    : epoll_server_(epoll_server) {}

QuicEpollAlarmFactory::~QuicEpollAlarmFactory() {}

QuicAlarm* QuicEpollAlarmFactory::CreateAlarm(QuicAlarm::Delegate* delegate) {
  return new QuicEpollAlarm(epoll_server_,
                            QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate));
}

QuicArenaScopedPtr<QuicAlarm> QuicEpollAlarmFactory::CreateAlarm(
    QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
    QuicConnectionArena* arena) {
  if (arena != nullptr) {
    return arena->New<QuicEpollAlarm>(epoll_server_, std::move(delegate));
  } else {
    return QuicArenaScopedPtr<QuicAlarm>(
        new QuicEpollAlarm(epoll_server_, std::move(delegate)));
  }
}

}  // namespace net
