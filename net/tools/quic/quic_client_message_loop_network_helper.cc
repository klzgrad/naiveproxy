// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_client_message_loop_network_helper.h"

#include <utility>

#include "base/logging.h"
#include "base/run_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_info.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_with_source.h"
#include "net/quic/chromium/quic_chromium_alarm_factory.h"
#include "net/quic/chromium/quic_chromium_connection_helper.h"
#include "net/quic/chromium/quic_chromium_packet_reader.h"
#include "net/quic/chromium/quic_chromium_packet_writer.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_server_id.h"
#include "net/quic/core/spdy_utils.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/socket/udp_client_socket.h"
#include "net/spdy/chromium/spdy_http_utils.h"
#include "net/spdy/core/spdy_header_block.h"

using std::string;

namespace net {

QuicClientMessageLooplNetworkHelper::QuicClientMessageLooplNetworkHelper(
    QuicChromiumClock* clock,
    QuicClientBase* client)
    : packet_reader_started_(false), clock_(clock), client_(client) {}

QuicClientMessageLooplNetworkHelper::~QuicClientMessageLooplNetworkHelper() =
    default;

bool QuicClientMessageLooplNetworkHelper::CreateUDPSocketAndBind(
    QuicSocketAddress server_address,
    QuicIpAddress bind_to_address,
    int bind_to_port) {
  std::unique_ptr<UDPClientSocket> socket(
      new UDPClientSocket(DatagramSocket::DEFAULT_BIND, RandIntCallback(),
                          &net_log_, NetLogSource()));

  if (bind_to_address.IsInitialized()) {
    client_address_ = QuicSocketAddress(bind_to_address, client_->local_port());
  } else if (server_address.host().address_family() == IpAddressFamily::IP_V4) {
    client_address_ = QuicSocketAddress(QuicIpAddress::Any4(), bind_to_port);
  } else {
    client_address_ = QuicSocketAddress(QuicIpAddress::Any6(), bind_to_port);
  }

  int rc = socket->Connect(server_address.impl().socket_address());
  if (rc != OK) {
    LOG(ERROR) << "Connect failed: " << ErrorToShortString(rc);
    return false;
  }

  rc = socket->SetReceiveBufferSize(kDefaultSocketReceiveBuffer);
  if (rc != OK) {
    LOG(ERROR) << "SetReceiveBufferSize() failed: " << ErrorToShortString(rc);
    return false;
  }

  rc = socket->SetSendBufferSize(kDefaultSocketReceiveBuffer);
  if (rc != OK) {
    LOG(ERROR) << "SetSendBufferSize() failed: " << ErrorToShortString(rc);
    return false;
  }

  IPEndPoint address;
  rc = socket->GetLocalAddress(&address);
  if (rc != OK) {
    LOG(ERROR) << "GetLocalAddress failed: " << ErrorToShortString(rc);
    return false;
  }
  client_address_ = QuicSocketAddress(QuicSocketAddressImpl(address));

  socket_.swap(socket);
  packet_reader_.reset(new QuicChromiumPacketReader(
      socket_.get(), clock_, this, kQuicYieldAfterPacketsRead,
      QuicTime::Delta::FromMilliseconds(kQuicYieldAfterDurationMilliseconds),
      NetLogWithSource()));

  if (socket != nullptr) {
    socket->Close();
  }

  return true;
}

void QuicClientMessageLooplNetworkHelper::CleanUpAllUDPSockets() {
  client_->reset_writer();
  packet_reader_.reset();
  packet_reader_started_ = false;
}

void QuicClientMessageLooplNetworkHelper::StartPacketReaderIfNotStarted() {
  if (!packet_reader_started_) {
    packet_reader_->StartReading();
    packet_reader_started_ = true;
  }
}

void QuicClientMessageLooplNetworkHelper::RunEventLoop() {
  StartPacketReaderIfNotStarted();
  base::RunLoop().RunUntilIdle();
}

QuicPacketWriter*
QuicClientMessageLooplNetworkHelper::CreateQuicPacketWriter() {
  return new QuicChromiumPacketWriter(
      socket_.get(), base::ThreadTaskRunnerHandle::Get().get());
}

void QuicClientMessageLooplNetworkHelper::OnReadError(
    int result,
    const DatagramClientSocket* socket) {
  LOG(ERROR) << "QuicSimpleClient read failed: " << ErrorToShortString(result);
  client_->Disconnect();
}

QuicSocketAddress QuicClientMessageLooplNetworkHelper::GetLatestClientAddress()
    const {
  return client_address_;
}

bool QuicClientMessageLooplNetworkHelper::OnPacket(
    const QuicReceivedPacket& packet,
    const QuicSocketAddress& local_address,
    const QuicSocketAddress& peer_address) {
  client_->session()->connection()->ProcessUdpPacket(local_address,
                                                     peer_address, packet);
  if (!client_->session()->connection()->connected()) {
    return false;
  }

  return true;
}

}  // namespace net
