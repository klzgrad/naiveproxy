// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// An implementation of the QuicClientBase::NetworkHelper
// that is based off the epoll server.

#ifndef NET_TOOLS_QUIC_QUIC_CLIENT_EPOLL_NETWORK_HELPER_H_
#define NET_TOOLS_QUIC_QUIC_CLIENT_EPOLL_NETWORK_HELPER_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/macros.h"
#include "net/quic/core/quic_client_push_promise_index.h"
#include "net/quic/core/quic_config.h"
#include "net/quic/core/quic_spdy_stream.h"
#include "net/quic/platform/api/quic_containers.h"
#include "net/tools/epoll_server/epoll_server.h"
#include "net/tools/quic/quic_client_base.h"
#include "net/tools/quic/quic_packet_reader.h"
#include "net/tools/quic/quic_process_packet_interface.h"
#include "net/tools/quic/quic_spdy_client_base.h"
#include "net/tools/quic/quic_spdy_client_session.h"

namespace net {

namespace test {
class QuicClientPeer;
}  // namespace test

// An implementation of the QuicClientBase::NetworkHelper based off
// the epoll server.
class QuicClientEpollNetworkHelper : public QuicClientBase::NetworkHelper,
                                     public EpollCallbackInterface,
                                     public ProcessPacketInterface {
 public:
  // Create a quic client, which will have events managed by an externally owned
  // EpollServer.
  QuicClientEpollNetworkHelper(EpollServer* epoll_server,
                               QuicClientBase* client);

  ~QuicClientEpollNetworkHelper() override;

  // From EpollCallbackInterface
  void OnRegistration(EpollServer* eps, int fd, int event_mask) override;
  void OnModification(int fd, int event_mask) override;
  void OnEvent(int fd, EpollEvent* event) override;
  // |fd_| can be unregistered without the client being disconnected. This
  // happens in b3m QuicProber where we unregister |fd_| to feed in events to
  // the client from the SelectServer.
  void OnUnregistration(int fd, bool replaced) override;
  void OnShutdown(EpollServer* eps, int fd) override;

  // From ProcessPacketInterface. This will be called for each received
  // packet.
  void ProcessPacket(const QuicSocketAddress& self_address,
                     const QuicSocketAddress& peer_address,
                     const QuicReceivedPacket& packet) override;

  // From NetworkHelper.
  void RunEventLoop() override;
  bool CreateUDPSocketAndBind(QuicSocketAddress server_address,
                              QuicIpAddress bind_to_address,
                              int bind_to_port) override;
  void CleanUpAllUDPSockets() override;
  QuicSocketAddress GetLatestClientAddress() const override;
  QuicPacketWriter* CreateQuicPacketWriter() override;

  // Accessors provided for convenience, not part of any interface.

  EpollServer* epoll_server() { return epoll_server_; }

  const QuicLinkedHashMap<int, QuicSocketAddress>& fd_address_map() const {
    return fd_address_map_;
  }

  // If the client has at least one UDP socket, return the latest created one.
  // Otherwise, return -1.
  int GetLatestFD() const;

  QuicClientBase* client() { return client_; }

  void set_max_reads_per_epoll_loop(int num_reads) {
    max_reads_per_epoll_loop_ = num_reads;
  }

 private:
  friend class test::QuicClientPeer;

  // Used for testing.
  void SetClientPort(int port);

  // If |fd| is an open UDP socket, unregister and close it. Otherwise, do
  // nothing.
  void CleanUpUDPSocket(int fd);

  // Actually clean up |fd|.
  void CleanUpUDPSocketImpl(int fd);

  // Listens for events on the client socket.
  EpollServer* epoll_server_;

  // Map mapping created UDP sockets to their addresses. By using linked hash
  // map, the order of socket creation can be recorded.
  QuicLinkedHashMap<int, QuicSocketAddress> fd_address_map_;

  // If overflow_supported_ is true, this will be the number of packets dropped
  // during the lifetime of the server.
  QuicPacketCount packets_dropped_;

  // True if the kernel supports SO_RXQ_OVFL, the number of packets dropped
  // because the socket would otherwise overflow.
  bool overflow_supported_;

  // Point to a QuicPacketReader object on the heap. The reader allocates more
  // space than allowed on the stack.
  std::unique_ptr<QuicPacketReader> packet_reader_;

  QuicClientBase* client_;

  int max_reads_per_epoll_loop_;

  DISALLOW_COPY_AND_ASSIGN(QuicClientEpollNetworkHelper);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_CLIENT_EPOLL_NETWORK_HELPER_H_
