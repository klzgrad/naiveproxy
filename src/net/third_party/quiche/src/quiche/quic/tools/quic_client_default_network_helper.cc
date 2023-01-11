// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_client_default_network_helper.h"

#include "absl/cleanup/cleanup.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_packet_writer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_udp_socket.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"

namespace quic {

namespace {

// For level-triggered I/O, we need to manually rearm the kSocketEventWritable
// listener whenever the socket gets blocked.
class LevelTriggeredPacketWriter : public QuicDefaultPacketWriter {
 public:
  explicit LevelTriggeredPacketWriter(int fd, QuicEventLoop* event_loop)
      : QuicDefaultPacketWriter(fd), event_loop_(event_loop) {
    QUICHE_DCHECK(!event_loop->SupportsEdgeTriggered());
  }

  WriteResult WritePacket(const char* buffer, size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override {
    WriteResult result = QuicDefaultPacketWriter::WritePacket(
        buffer, buf_len, self_address, peer_address, options);
    if (IsWriteBlockedStatus(result.status)) {
      bool success = event_loop_->RearmSocket(fd(), kSocketEventWritable);
      QUICHE_DCHECK(success);
    }
    return result;
  }

 private:
  QuicEventLoop* event_loop_;
};

}  // namespace

QuicClientDefaultNetworkHelper::QuicClientDefaultNetworkHelper(
    QuicEventLoop* event_loop, QuicClientBase* client)
    : event_loop_(event_loop),
      packets_dropped_(0),
      overflow_supported_(false),
      packet_reader_(new QuicPacketReader()),
      client_(client),
      max_reads_per_event_loop_(std::numeric_limits<int>::max()) {}

QuicClientDefaultNetworkHelper::~QuicClientDefaultNetworkHelper() {
  if (client_->connected()) {
    client_->session()->connection()->CloseConnection(
        QUIC_PEER_GOING_AWAY, "Client being torn down",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  CleanUpAllUDPSockets();
}

bool QuicClientDefaultNetworkHelper::CreateUDPSocketAndBind(
    QuicSocketAddress server_address, QuicIpAddress bind_to_address,
    int bind_to_port) {
  int fd = CreateUDPSocket(server_address, &overflow_supported_);
  if (fd < 0) {
    return false;
  }
  auto closer = absl::MakeCleanup([fd] { close(fd); });

  QuicSocketAddress client_address;
  if (bind_to_address.IsInitialized()) {
    client_address = QuicSocketAddress(bind_to_address, client_->local_port());
  } else if (server_address.host().address_family() == IpAddressFamily::IP_V4) {
    client_address = QuicSocketAddress(QuicIpAddress::Any4(), bind_to_port);
  } else {
    client_address = QuicSocketAddress(QuicIpAddress::Any6(), bind_to_port);
  }

  // Some platforms expect that the addrlen given to bind() exactly matches the
  // size of the associated protocol family's sockaddr struct.
  // TODO(b/179430548): Revert this when affected platforms are updated to
  // to support binding with an addrelen of sizeof(sockaddr_storage)
  socklen_t addrlen;
  switch (client_address.host().address_family()) {
    case IpAddressFamily::IP_V4:
      addrlen = sizeof(sockaddr_in);
      break;
    case IpAddressFamily::IP_V6:
      addrlen = sizeof(sockaddr_in6);
      break;
    case IpAddressFamily::IP_UNSPEC:
      addrlen = 0;
      break;
  }

  sockaddr_storage addr = client_address.generic_address();
  int rc = bind(fd, reinterpret_cast<sockaddr*>(&addr), addrlen);
  if (rc < 0) {
    QUIC_LOG(ERROR) << "Bind failed: " << strerror(errno)
                    << " bind_to_address:" << bind_to_address
                    << ", bind_to_port:" << bind_to_port
                    << ", client_address:" << client_address;
    return false;
  }

  if (client_address.FromSocket(fd) != 0) {
    QUIC_LOG(ERROR) << "Unable to get self address.  Error: "
                    << strerror(errno);
  }

  if (event_loop_->RegisterSocket(
          fd, kSocketEventReadable | kSocketEventWritable, this)) {
    fd_address_map_[fd] = client_address;
    std::move(closer).Cancel();
    return true;
  }
  return false;
}

void QuicClientDefaultNetworkHelper::CleanUpUDPSocket(int fd) {
  CleanUpUDPSocketImpl(fd);
  fd_address_map_.erase(fd);
}

void QuicClientDefaultNetworkHelper::CleanUpAllUDPSockets() {
  for (std::pair<int, QuicSocketAddress> fd_address : fd_address_map_) {
    CleanUpUDPSocketImpl(fd_address.first);
  }
  fd_address_map_.clear();
}

void QuicClientDefaultNetworkHelper::CleanUpUDPSocketImpl(int fd) {
  if (fd > -1) {
    bool success = event_loop_->UnregisterSocket(fd);
    QUICHE_DCHECK(success || fds_unregistered_externally_);
    int rc = close(fd);
    QUICHE_DCHECK_EQ(0, rc);
  }
}

void QuicClientDefaultNetworkHelper::RunEventLoop() {
  quiche::QuicheRunSystemEventLoopIteration();
  event_loop_->RunEventLoopOnce(QuicTime::Delta::FromMilliseconds(50));
}

void QuicClientDefaultNetworkHelper::OnSocketEvent(
    QuicEventLoop* /*event_loop*/, QuicUdpSocketFd fd,
    QuicSocketEventMask events) {
  if (events & kSocketEventReadable) {
    QUIC_DVLOG(1) << "Read packets on kSocketEventReadable";
    int times_to_read = max_reads_per_event_loop_;
    bool more_to_read = true;
    QuicPacketCount packets_dropped = 0;
    while (client_->connected() && more_to_read && times_to_read > 0) {
      more_to_read = packet_reader_->ReadAndDispatchPackets(
          fd, GetLatestClientAddress().port(), *client_->helper()->GetClock(),
          this, overflow_supported_ ? &packets_dropped : nullptr);
      --times_to_read;
    }
    if (packets_dropped_ < packets_dropped) {
      QUIC_LOG(ERROR)
          << packets_dropped - packets_dropped_
          << " more packets are dropped in the socket receive buffer.";
      packets_dropped_ = packets_dropped;
    }
    if (client_->connected() && more_to_read) {
      bool success =
          event_loop_->ArtificiallyNotifyEvent(fd, kSocketEventReadable);
      QUICHE_DCHECK(success);
    } else if (!event_loop_->SupportsEdgeTriggered()) {
      bool success = event_loop_->RearmSocket(fd, kSocketEventReadable);
      QUICHE_DCHECK(success);
    }
  }
  if (client_->connected() && (events & kSocketEventWritable)) {
    client_->writer()->SetWritable();
    client_->session()->connection()->OnCanWrite();
  }
}

QuicPacketWriter* QuicClientDefaultNetworkHelper::CreateQuicPacketWriter() {
  if (event_loop_->SupportsEdgeTriggered()) {
    return new QuicDefaultPacketWriter(GetLatestFD());
  } else {
    return new LevelTriggeredPacketWriter(GetLatestFD(), event_loop_);
  }
}

void QuicClientDefaultNetworkHelper::SetClientPort(int port) {
  fd_address_map_.back().second =
      QuicSocketAddress(GetLatestClientAddress().host(), port);
}

QuicSocketAddress QuicClientDefaultNetworkHelper::GetLatestClientAddress()
    const {
  if (fd_address_map_.empty()) {
    return QuicSocketAddress();
  }

  return fd_address_map_.back().second;
}

int QuicClientDefaultNetworkHelper::GetLatestFD() const {
  if (fd_address_map_.empty()) {
    return -1;
  }

  return fd_address_map_.back().first;
}

void QuicClientDefaultNetworkHelper::ProcessPacket(
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address, const QuicReceivedPacket& packet) {
  client_->session()->ProcessUdpPacket(self_address, peer_address, packet);
}

int QuicClientDefaultNetworkHelper::CreateUDPSocket(
    QuicSocketAddress server_address, bool* overflow_supported) {
  QuicUdpSocketApi api;
  int fd = api.Create(server_address.host().AddressFamilyToInt(),
                      /*receive_buffer_size =*/kDefaultSocketReceiveBuffer,
                      /*send_buffer_size =*/kDefaultSocketReceiveBuffer);
  if (fd < 0) {
    return fd;
  }

  *overflow_supported = api.EnableDroppedPacketCount(fd);
  api.EnableReceiveTimestamp(fd);

  if (!BindInterfaceNameIfNeeded(fd)) {
    CleanUpUDPSocket(fd);
    return kQuicInvalidSocketFd;
  }

  return fd;
}

bool QuicClientDefaultNetworkHelper::BindInterfaceNameIfNeeded(int fd) {
  QuicUdpSocketApi api;
  std::string interface_name = client_->interface_name();
  if (!interface_name.empty()) {
    if (!api.BindInterface(fd, interface_name)) {
      QUIC_DLOG(WARNING) << "Failed to bind socket (" << fd
                         << ") to interface (" << interface_name << ").";
      return false;
    }
  }
  return true;
}

}  // namespace quic
