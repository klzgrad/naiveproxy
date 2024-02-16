// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_encapsulated_client_session.h"

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/frames/quic_connection_close_frame.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/masque/masque_client_session.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/capsule.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_ip_address.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {

using ::quiche::AddressAssignCapsule;
using ::quiche::AddressRequestCapsule;
using ::quiche::RouteAdvertisementCapsule;

MasqueEncapsulatedClientSession::MasqueEncapsulatedClientSession(
    const QuicConfig& config, const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection, const QuicServerId& server_id,
    QuicCryptoClientConfig* crypto_config,
    MasqueClientSession* masque_client_session)
    : QuicSpdyClientSession(config, supported_versions, connection, server_id,
                            crypto_config),
      masque_client_session_(masque_client_session) {}

void MasqueEncapsulatedClientSession::ProcessPacket(
    absl::string_view packet, QuicSocketAddress server_address) {
  QuicTime now = connection()->clock()->ApproximateNow();
  QuicReceivedPacket received_packet(packet.data(), packet.length(), now);
  connection()->ProcessUdpPacket(connection()->self_address(), server_address,
                                 received_packet);
}

void MasqueEncapsulatedClientSession::CloseConnection(
    QuicErrorCode error, const std::string& details,
    ConnectionCloseBehavior connection_close_behavior) {
  connection()->CloseConnection(error, details, connection_close_behavior);
}

void MasqueEncapsulatedClientSession::OnConnectionClosed(
    const QuicConnectionCloseFrame& frame, ConnectionCloseSource source) {
  QuicSpdyClientSession::OnConnectionClosed(frame, source);
  masque_client_session_->CloseConnectUdpStream(this);
}

void MasqueEncapsulatedClientSession::ProcessIpPacket(
    absl::string_view packet) {
  quiche::QuicheDataReader reader(packet);
  uint8_t first_byte;
  if (!reader.ReadUInt8(&first_byte)) {
    QUIC_DLOG(ERROR) << "Dropping empty CONNECT-IP packet";
    return;
  }
  const uint8_t ip_version = first_byte >> 4;
  quiche::QuicheIpAddress server_ip;
  if (ip_version == 6) {
    if (!reader.Seek(5)) {
      QUICHE_DLOG(ERROR) << "Failed to seek CONNECT-IP IPv6 start"
                         << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    uint8_t next_header = 0;
    if (!reader.ReadUInt8(&next_header)) {
      QUICHE_DLOG(ERROR) << "Failed to read CONNECT-IP next header"
                         << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    if (next_header != 17) {
      // Note that this drops packets with IPv6 extension headers, since we
      // do not expect to see them in practice.
      QUIC_DLOG(ERROR)
          << "Dropping CONNECT-IP packet with unexpected next header "
          << static_cast<int>(next_header) << "\n"
          << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    if (!reader.Seek(1)) {
      QUICHE_DLOG(ERROR) << "Failed to seek CONNECT-IP hop limit"
                         << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    absl::string_view source_ip;
    if (!reader.ReadStringPiece(&source_ip, 16)) {
      QUICHE_DLOG(ERROR) << "Failed to read CONNECT-IP source IPv6"
                         << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    server_ip.FromPackedString(source_ip.data(), source_ip.length());
    if (!reader.Seek(16)) {
      QUICHE_DLOG(ERROR) << "Failed to seek CONNECT-IP destination IPv6"
                         << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
  } else if (ip_version == 4) {
    uint8_t ihl = first_byte & 0xF;
    if (ihl < 5) {
      QUICHE_DLOG(ERROR) << "Dropping CONNECT-IP packet with invalid IHL "
                         << static_cast<int>(ihl) << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    if (!reader.Seek(8)) {
      QUICHE_DLOG(ERROR) << "Failed to seek CONNECT-IP IPv4 start"
                         << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    uint8_t ip_proto = 0;
    if (!reader.ReadUInt8(&ip_proto)) {
      QUICHE_DLOG(ERROR) << "Failed to read CONNECT-IP ip_proto"
                         << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    if (ip_proto != 17) {
      QUIC_DLOG(ERROR) << "Dropping CONNECT-IP packet with unexpected IP proto "
                       << static_cast<int>(ip_proto) << "\n"
                       << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    if (!reader.Seek(2)) {
      QUICHE_DLOG(ERROR) << "Failed to seek CONNECT-IP IP checksum"
                         << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    absl::string_view source_ip;
    if (!reader.ReadStringPiece(&source_ip, 4)) {
      QUICHE_DLOG(ERROR) << "Failed to read CONNECT-IP source IPv4"
                         << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    server_ip.FromPackedString(source_ip.data(), source_ip.length());
    if (!reader.Seek(4)) {
      QUICHE_DLOG(ERROR) << "Failed to seek CONNECT-IP destination IPv4"
                         << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
    uint8_t ip_options_length = (ihl - 5) * 4;
    if (!reader.Seek(ip_options_length)) {
      QUICHE_DLOG(ERROR) << "Failed to seek CONNECT-IP IP options of length "
                         << static_cast<int>(ip_options_length) << "\n"
                         << quiche::QuicheTextUtils::HexDump(packet);
      return;
    }
  } else {
    QUIC_DLOG(ERROR) << "Dropping CONNECT-IP packet with unexpected IP version "
                     << static_cast<int>(ip_version) << "\n"
                     << quiche::QuicheTextUtils::HexDump(packet);
    return;
  }
  // Parse UDP header.
  uint16_t server_port;
  if (!reader.ReadUInt16(&server_port)) {
    QUICHE_DLOG(ERROR) << "Failed to read CONNECT-IP source port"
                       << "\n"
                       << quiche::QuicheTextUtils::HexDump(packet);
    return;
  }
  if (!reader.Seek(2)) {
    QUICHE_DLOG(ERROR) << "Failed to seek CONNECT-IP destination port"
                       << "\n"
                       << quiche::QuicheTextUtils::HexDump(packet);
    return;
  }
  uint16_t udp_length;
  if (!reader.ReadUInt16(&udp_length)) {
    QUICHE_DLOG(ERROR) << "Failed to read CONNECT-IP UDP length"
                       << "\n"
                       << quiche::QuicheTextUtils::HexDump(packet);
    return;
  }
  if (udp_length < 8) {
    QUICHE_DLOG(ERROR) << "Dropping CONNECT-IP packet with invalid UDP length "
                       << udp_length << "\n"
                       << quiche::QuicheTextUtils::HexDump(packet);
    return;
  }
  if (!reader.Seek(2)) {
    QUICHE_DLOG(ERROR) << "Failed to seek CONNECT-IP UDP checksum"
                       << "\n"
                       << quiche::QuicheTextUtils::HexDump(packet);
    return;
  }
  absl::string_view quic_packet;
  if (!reader.ReadStringPiece(&quic_packet, udp_length - 8)) {
    QUICHE_DLOG(ERROR) << "Failed to read CONNECT-IP UDP payload"
                       << "\n"
                       << quiche::QuicheTextUtils::HexDump(packet);
    return;
  }
  if (!reader.IsDoneReading()) {
    QUICHE_DLOG(INFO) << "Received CONNECT-IP UDP packet with "
                      << reader.BytesRemaining()
                      << " extra bytes after payload\n"
                      << quiche::QuicheTextUtils::HexDump(packet);
  }
  QUIC_DLOG(INFO) << "Received CONNECT-IP encapsulated packet of length "
                  << quic_packet.size();
  QuicTime now = connection()->clock()->ApproximateNow();
  QuicReceivedPacket received_packet(quic_packet.data(), quic_packet.size(),
                                     now);
  QuicSocketAddress server_address = QuicSocketAddress(server_ip, server_port);
  connection()->ProcessUdpPacket(connection()->self_address(), server_address,
                                 received_packet);
}

void MasqueEncapsulatedClientSession::CloseIpSession(
    const std::string& details) {
  connection()->CloseConnection(QUIC_CONNECTION_CANCELLED, details,
                                ConnectionCloseBehavior::SILENT_CLOSE);
}

bool MasqueEncapsulatedClientSession::OnAddressAssignCapsule(
    const AddressAssignCapsule& capsule) {
  QUIC_DLOG(INFO) << "Received capsule " << capsule.ToString();
  for (auto assigned_address : capsule.assigned_addresses) {
    if (assigned_address.ip_prefix.address().IsIPv4() &&
        !local_v4_address_.IsInitialized()) {
      QUIC_LOG(INFO)
          << "MasqueEncapsulatedClientSession saving local IPv4 address "
          << assigned_address.ip_prefix.address();
      local_v4_address_ = assigned_address.ip_prefix.address();
    } else if (assigned_address.ip_prefix.address().IsIPv6() &&
               !local_v6_address_.IsInitialized()) {
      QUIC_LOG(INFO)
          << "MasqueEncapsulatedClientSession saving local IPv6 address "
          << assigned_address.ip_prefix.address();
      local_v6_address_ = assigned_address.ip_prefix.address();
    }
  }
  return true;
}

bool MasqueEncapsulatedClientSession::OnAddressRequestCapsule(
    const AddressRequestCapsule& capsule) {
  QUIC_DLOG(INFO) << "Ignoring received capsule " << capsule.ToString();
  return true;
}

bool MasqueEncapsulatedClientSession::OnRouteAdvertisementCapsule(
    const RouteAdvertisementCapsule& capsule) {
  QUIC_DLOG(INFO) << "Ignoring received capsule " << capsule.ToString();
  return true;
}

}  // namespace quic
