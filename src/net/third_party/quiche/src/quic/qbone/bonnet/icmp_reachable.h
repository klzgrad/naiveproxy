// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_ICMP_REACHABLE_H_
#define QUICHE_QUIC_QBONE_BONNET_ICMP_REACHABLE_H_

#include <netinet/icmp6.h>

#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mutex.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/qbone/bonnet/icmp_reachable_interface.h"
#include "net/third_party/quiche/src/quic/qbone/platform/kernel_interface.h"

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
    absl::Duration response_time;
    string source;
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
  IcmpReachable(QuicIpAddress source,
                QuicIpAddress destination,
                absl::Duration timeout,
                KernelInterface* kernel,
                QuicEpollServer* epoll_server,
                StatsInterface* stats);

  ~IcmpReachable() override;

  // Initializes this reachability probe. Must be called from within the
  // |epoll_server|'s thread.
  bool Init() QUIC_LOCKS_EXCLUDED(header_lock_) override;

  int64 /* allow-non-std-int */ OnAlarm()
      QUIC_LOCKS_EXCLUDED(header_lock_) override;

  static QuicStringPiece StatusName(Status status);

 private:
  class EpollCallback : public QuicEpollCallbackInterface {
   public:
    explicit EpollCallback(IcmpReachable* reachable) : reachable_(reachable) {}

    EpollCallback(const EpollCallback&) = delete;
    EpollCallback& operator=(const EpollCallback&) = delete;

    EpollCallback(EpollCallback&&) = delete;
    EpollCallback& operator=(EpollCallback&&) = delete;

    void OnRegistration(QuicEpollServer* eps,
                        int fd,
                        int event_mask) override{};

    void OnModification(int fd, int event_mask) override{};

    void OnEvent(int fd, QuicEpollEvent* event) override;

    void OnUnregistration(int fd, bool replaced) override{};

    void OnShutdown(QuicEpollServer* eps, int fd) override;

    string Name() const override;

   private:
    IcmpReachable* reachable_;
  };

  bool OnEvent(int fd) QUIC_LOCKS_EXCLUDED(header_lock_);

  const absl::Duration timeout_;

  EpollCallback cb_;

  sockaddr_in6 src_{};
  sockaddr_in6 dst_{};

  KernelInterface* kernel_;
  QuicEpollServer* epoll_server_;

  StatsInterface* stats_;

  int send_fd_;
  int recv_fd_;

  QuicMutex header_lock_;
  icmp6_hdr icmp_header_ QUIC_GUARDED_BY(header_lock_){};

  absl::Time start_ = absl::InfinitePast();
  absl::Time end_ = absl::InfinitePast();
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_ICMP_REACHABLE_H_
