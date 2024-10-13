// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_ICMP_REACHABLE_H_
#define QUICHE_QUIC_QBONE_BONNET_ICMP_REACHABLE_H_

#include <netinet/icmp6.h>

#include <memory>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/qbone/bonnet/icmp_reachable_interface.h"
#include "quiche/quic/qbone/platform/kernel_interface.h"
#include "quiche/common/platform/api/quiche_mutex.h"

namespace quic {

extern const char kUnknownSource[];
extern const char kNoSource[];

// IcmpReachable schedules itself with an EpollServer, periodically sending
// ICMPv6 Echo Requests to the given |destination| on the interface that the
// given |source| is bound to. Echo Requests are sent once every |timeout|.
// On Echo Replies, timeouts, and I/O errors, the given |stats| object will
// be called back with details of the event.
class IcmpReachable : public IcmpReachableInterface {
 public:
  enum Status { REACHABLE, UNREACHABLE };

  struct ReachableEvent {
    Status status;
    QuicTime::Delta response_time;
    std::string source;
  };

  class StatsInterface {
   public:
    StatsInterface() = default;

    StatsInterface(const StatsInterface&) = delete;
    StatsInterface& operator=(const StatsInterface&) = delete;

    StatsInterface(StatsInterface&&) = delete;
    StatsInterface& operator=(StatsInterface&&) = delete;

    virtual ~StatsInterface() = default;

    virtual void OnEvent(ReachableEvent event) = 0;

    virtual void OnReadError(int error) = 0;

    virtual void OnWriteError(int error) = 0;
  };

  // |source| is the IPv6 address bound to the interface that IcmpReachable will
  //          send Echo Requests on.
  // |destination| is the IPv6 address of the destination of the Echo Requests.
  // |timeout| is the duration IcmpReachable will wait between Echo Requests.
  //           If no Echo Response is received by the next Echo Request, it will
  //           be considered a timeout.
  // |kernel| is not owned, but should outlive this instance.
  // |epoll_server| is not owned, but should outlive this instance.
  //                IcmpReachable's Init() must be called from within the Epoll
  //                Server's thread.
  // |stats| is not owned, but should outlive this instance. It will be called
  //         back on Echo Replies, timeouts, and I/O errors.
  IcmpReachable(QuicIpAddress source, QuicIpAddress destination,
                QuicTime::Delta timeout, KernelInterface* kernel,
                QuicEventLoop* event_loop, StatsInterface* stats);

  ~IcmpReachable() override;

  // Initializes this reachability probe. Must be called from within the
  // |epoll_server|'s thread.
  bool Init() QUICHE_LOCKS_EXCLUDED(header_lock_) override;

  void OnAlarm() QUICHE_LOCKS_EXCLUDED(header_lock_);

  static absl::string_view StatusName(Status status);

 private:
  class EpollCallback : public QuicSocketEventListener {
   public:
    explicit EpollCallback(IcmpReachable* reachable) : reachable_(reachable) {}

    EpollCallback(const EpollCallback&) = delete;
    EpollCallback& operator=(const EpollCallback&) = delete;

    EpollCallback(EpollCallback&&) = delete;
    EpollCallback& operator=(EpollCallback&&) = delete;

    void OnSocketEvent(QuicEventLoop* event_loop, SocketFd fd,
                       QuicSocketEventMask events) override;

   private:
    IcmpReachable* reachable_;
  };

  class AlarmCallback : public QuicAlarm::DelegateWithoutContext {
   public:
    explicit AlarmCallback(IcmpReachable* reachable) : reachable_(reachable) {}

    void OnAlarm() override { reachable_->OnAlarm(); }

   private:
    IcmpReachable* reachable_;
  };

  bool OnEvent(int fd) QUICHE_LOCKS_EXCLUDED(header_lock_);

  const QuicTime::Delta timeout_;

  QuicEventLoop* event_loop_;
  const QuicClock* clock_;
  std::unique_ptr<QuicAlarmFactory> alarm_factory_;

  EpollCallback cb_;
  std::unique_ptr<QuicAlarm> alarm_;

  sockaddr_in6 src_{};
  sockaddr_in6 dst_{};

  KernelInterface* kernel_;

  StatsInterface* stats_;

  int send_fd_;
  int recv_fd_;

  quiche::QuicheMutex header_lock_;
  icmp6_hdr icmp_header_ QUICHE_GUARDED_BY(header_lock_){};

  QuicTime start_ = QuicTime::Zero();
  QuicTime end_ = QuicTime::Zero();
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_ICMP_REACHABLE_H_
