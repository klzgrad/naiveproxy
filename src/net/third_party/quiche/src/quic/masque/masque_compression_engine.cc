// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/masque/masque_compression_engine.h"

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/quic_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {

namespace {
// |kFlowId0| is used to indicate creation of a new compression context.
const QuicDatagramFlowId kFlowId0 = 0;

enum MasqueAddressFamily : uint8_t {
  MasqueAddressFamilyIPv4 = 4,
  MasqueAddressFamilyIPv6 = 6,
};

}  // namespace

MasqueCompressionEngine::MasqueCompressionEngine(QuicSession* masque_session)
    : masque_session_(masque_session) {
  if (masque_session_->perspective() == Perspective::IS_SERVER) {
    next_flow_id_ = 1;
  } else {
    next_flow_id_ = 2;
  }
}

QuicDatagramFlowId MasqueCompressionEngine::FindOrCreateCompressionContext(
    QuicConnectionId client_connection_id,
    QuicConnectionId server_connection_id,
    const QuicSocketAddress& server_address,
    bool client_connection_id_present,
    bool server_connection_id_present,
    bool* validated) {
  QuicDatagramFlowId flow_id = kFlowId0;
  *validated = false;
  for (const auto& kv : contexts_) {
    const MasqueCompressionContext& context = kv.second;
    if (context.server_address != server_address) {
      continue;
    }
    if (client_connection_id_present &&
        context.client_connection_id != client_connection_id) {
      continue;
    }
    if (server_connection_id_present &&
        context.server_connection_id != server_connection_id) {
      continue;
    }

    flow_id = kv.first;
    DCHECK_NE(flow_id, kFlowId0);
    *validated = context.validated;
    QUIC_DVLOG(1) << "Compressing using " << (*validated ? "" : "un")
                  << "validated flow_id " << flow_id << " to "
                  << context.server_address << " client "
                  << context.client_connection_id << " server "
                  << context.server_connection_id;
    break;
  }

  if (flow_id != kFlowId0) {
    // Found a compression context, use it.
    return flow_id;
  }

  // Create new compression context.
  flow_id = GetNextFlowId();
  QUIC_DVLOG(1) << "Compression assigning new flow_id " << flow_id << " to "
                << server_address << " client " << client_connection_id
                << " server " << server_connection_id;
  MasqueCompressionContext context;
  context.client_connection_id = client_connection_id;
  context.server_connection_id = server_connection_id;
  context.server_address = server_address;
  contexts_[flow_id] = context;

  return flow_id;
}

bool MasqueCompressionEngine::WriteCompressedPacketToSlice(
    QuicConnectionId client_connection_id,
    QuicConnectionId server_connection_id,
    const QuicSocketAddress& server_address,
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id,
    QuicDatagramFlowId flow_id,
    bool validated,
    uint8_t first_byte,
    bool long_header,
    QuicDataReader* reader,
    QuicDataWriter* writer) {
  if (validated) {
    QUIC_DVLOG(1) << "Compressing using validated flow_id " << flow_id;
    if (!writer->WriteVarInt62(flow_id)) {
      QUIC_BUG << "Failed to write flow_id";
      return false;
    }
  } else {
    QUIC_DVLOG(1) << "Compressing using unvalidated flow_id " << flow_id;
    if (!writer->WriteVarInt62(kFlowId0)) {
      QUIC_BUG << "Failed to write kFlowId0";
      return false;
    }
    if (!writer->WriteVarInt62(flow_id)) {
      QUIC_BUG << "Failed to write flow_id";
      return false;
    }
    if (!writer->WriteLengthPrefixedConnectionId(client_connection_id)) {
      QUIC_BUG << "Failed to write client_connection_id";
      return false;
    }
    if (!writer->WriteLengthPrefixedConnectionId(server_connection_id)) {
      QUIC_BUG << "Failed to write server_connection_id";
      return false;
    }
    if (!writer->WriteUInt16(server_address.port())) {
      QUIC_BUG << "Failed to write port";
      return false;
    }
    QuicIpAddress peer_ip = server_address.host();
    DCHECK(peer_ip.IsInitialized());
    std::string peer_ip_bytes = peer_ip.ToPackedString();
    DCHECK(!peer_ip_bytes.empty());
    uint8_t address_id;
    if (peer_ip.address_family() == IpAddressFamily::IP_V6) {
      address_id = MasqueAddressFamilyIPv6;
      if (peer_ip_bytes.length() != QuicIpAddress::kIPv6AddressSize) {
        QUIC_BUG << "Bad IPv6 length " << server_address;
        return false;
      }
    } else if (peer_ip.address_family() == IpAddressFamily::IP_V4) {
      address_id = MasqueAddressFamilyIPv4;
      if (peer_ip_bytes.length() != QuicIpAddress::kIPv4AddressSize) {
        QUIC_BUG << "Bad IPv4 length " << server_address;
        return false;
      }
    } else {
      QUIC_BUG << "Unexpected server_address " << server_address;
      return false;
    }
    if (!writer->WriteUInt8(address_id)) {
      QUIC_BUG << "Failed to write address_id";
      return false;
    }
    if (!writer->WriteStringPiece(peer_ip_bytes)) {
      QUIC_BUG << "Failed to write IP address";
      return false;
    }
  }
  if (!writer->WriteUInt8(first_byte)) {
    QUIC_BUG << "Failed to write first_byte";
    return false;
  }
  if (long_header) {
    QuicVersionLabel version_label;
    if (!reader->ReadUInt32(&version_label)) {
      QUIC_DLOG(ERROR) << "Failed to read version";
      return false;
    }
    if (!writer->WriteUInt32(version_label)) {
      QUIC_BUG << "Failed to write version";
      return false;
    }
    QuicConnectionId packet_destination_connection_id,
        packet_source_connection_id;
    if (!reader->ReadLengthPrefixedConnectionId(
            &packet_destination_connection_id) ||
        !reader->ReadLengthPrefixedConnectionId(&packet_source_connection_id)) {
      QUIC_DLOG(ERROR) << "Failed to parse long header connection IDs";
      return false;
    }
    if (packet_destination_connection_id != destination_connection_id) {
      QUIC_DLOG(ERROR) << "Long header packet's destination_connection_id "
                       << packet_destination_connection_id
                       << " does not match expected "
                       << destination_connection_id;
      return false;
    }
    if (packet_source_connection_id != source_connection_id) {
      QUIC_DLOG(ERROR) << "Long header packet's source_connection_id "
                       << packet_source_connection_id
                       << " does not match expected " << source_connection_id;
      return false;
    }
  } else {
    QuicConnectionId packet_destination_connection_id;
    if (!reader->ReadConnectionId(&packet_destination_connection_id,
                                  destination_connection_id.length())) {
      QUIC_DLOG(ERROR)
          << "Failed to read short header packet's destination_connection_id";
      return false;
    }
    if (packet_destination_connection_id != destination_connection_id) {
      QUIC_DLOG(ERROR) << "Short header packet's destination_connection_id "
                       << packet_destination_connection_id
                       << " does not match expected "
                       << destination_connection_id;
      return false;
    }
  }
  quiche::QuicheStringPiece packet_payload = reader->ReadRemainingPayload();
  if (!writer->WriteStringPiece(packet_payload)) {
    QUIC_BUG << "Failed to write packet_payload";
    return false;
  }
  return true;
}

void MasqueCompressionEngine::CompressAndSendPacket(
    quiche::QuicheStringPiece packet,
    QuicConnectionId client_connection_id,
    QuicConnectionId server_connection_id,
    const QuicSocketAddress& server_address) {
  QUIC_DVLOG(2) << "Compressing client " << client_connection_id << " server "
                << server_connection_id << "\n"
                << quiche::QuicheTextUtils::HexDump(packet);
  DCHECK(server_address.IsInitialized());
  if (packet.empty()) {
    QUIC_BUG << "Tried to send empty packet";
    return;
  }
  QuicDataReader reader(packet.data(), packet.length());
  uint8_t first_byte;
  if (!reader.ReadUInt8(&first_byte)) {
    QUIC_BUG << "Failed to read first_byte";
    return;
  }
  const bool long_header = (first_byte & FLAGS_LONG_HEADER) != 0;
  bool client_connection_id_present = true, server_connection_id_present = true;
  QuicConnectionId destination_connection_id, source_connection_id;
  if (masque_session_->perspective() == Perspective::IS_SERVER) {
    destination_connection_id = client_connection_id;
    source_connection_id = server_connection_id;
    if (!long_header) {
      server_connection_id_present = false;
    }
  } else {
    destination_connection_id = server_connection_id;
    source_connection_id = client_connection_id;
    if (!long_header) {
      client_connection_id_present = false;
    }
  }

  bool validated = false;
  QuicDatagramFlowId flow_id = FindOrCreateCompressionContext(
      client_connection_id, server_connection_id, server_address,
      client_connection_id_present, server_connection_id_present, &validated);

  size_t slice_length = packet.length() - destination_connection_id.length();
  if (long_header) {
    slice_length -= sizeof(uint8_t) * 2 + source_connection_id.length();
  }
  if (validated) {
    slice_length += QuicDataWriter::GetVarInt62Len(flow_id);
  } else {
    slice_length += QuicDataWriter::GetVarInt62Len(kFlowId0) +
                    QuicDataWriter::GetVarInt62Len(flow_id) + sizeof(uint8_t) +
                    client_connection_id.length() + sizeof(uint8_t) +
                    server_connection_id.length() +
                    sizeof(server_address.port()) + sizeof(uint8_t) +
                    server_address.host().ToPackedString().length();
  }
  QuicUniqueBufferPtr buffer = MakeUniqueBuffer(
      masque_session_->connection()->helper()->GetStreamSendBufferAllocator(),
      slice_length);
  QuicDataWriter writer(slice_length, buffer.get());

  if (!WriteCompressedPacketToSlice(
          client_connection_id, server_connection_id, server_address,
          destination_connection_id, source_connection_id, flow_id, validated,
          first_byte, long_header, &reader, &writer)) {
    return;
  }

  QuicMemSlice slice(std::move(buffer), slice_length);
  MessageResult message_result =
      masque_session_->SendMessage(QuicMemSliceSpan(&slice));

  QUIC_DVLOG(1) << "Sent packet compressed with flow ID " << flow_id
                << " and got message result " << message_result;
}

bool MasqueCompressionEngine::ParseCompressionContext(
    QuicDataReader* reader,
    MasqueCompressionContext* context) {
  QuicDatagramFlowId new_flow_id;
  if (!reader->ReadVarInt62(&new_flow_id)) {
    QUIC_DLOG(ERROR) << "Could not read new_flow_id";
    return false;
  }
  QuicConnectionId new_client_connection_id;
  if (!reader->ReadLengthPrefixedConnectionId(&new_client_connection_id)) {
    QUIC_DLOG(ERROR) << "Could not read new_client_connection_id";
    return false;
  }
  QuicConnectionId new_server_connection_id;
  if (!reader->ReadLengthPrefixedConnectionId(&new_server_connection_id)) {
    QUIC_DLOG(ERROR) << "Could not read new_server_connection_id";
    return false;
  }
  uint16_t port;
  if (!reader->ReadUInt16(&port)) {
    QUIC_DLOG(ERROR) << "Could not read port";
    return false;
  }
  uint8_t address_id;
  if (!reader->ReadUInt8(&address_id)) {
    QUIC_DLOG(ERROR) << "Could not read address_id";
    return false;
  }
  size_t ip_bytes_length;
  if (address_id == MasqueAddressFamilyIPv6) {
    ip_bytes_length = QuicIpAddress::kIPv6AddressSize;
  } else if (address_id == MasqueAddressFamilyIPv4) {
    ip_bytes_length = QuicIpAddress::kIPv4AddressSize;
  } else {
    QUIC_DLOG(ERROR) << "Unknown address_id " << static_cast<int>(address_id);
    return false;
  }
  char ip_bytes[QuicIpAddress::kMaxAddressSize];
  if (!reader->ReadBytes(ip_bytes, ip_bytes_length)) {
    QUIC_DLOG(ERROR) << "Could not read IP address";
    return false;
  }
  QuicIpAddress ip_address;
  ip_address.FromPackedString(ip_bytes, ip_bytes_length);
  if (!ip_address.IsInitialized()) {
    QUIC_BUG << "Failed to parse IP address";
    return false;
  }
  QuicSocketAddress new_server_address = QuicSocketAddress(ip_address, port);
  auto context_pair = contexts_.find(new_flow_id);
  if (context_pair == contexts_.end()) {
    context->client_connection_id = new_client_connection_id;
    context->server_connection_id = new_server_connection_id;
    context->server_address = new_server_address;
    context->validated = true;
    contexts_[new_flow_id] = *context;
    QUIC_DVLOG(1) << "Registered new flow_id " << new_flow_id << " to "
                  << new_server_address << " client "
                  << new_client_connection_id << " server "
                  << new_server_connection_id;
  } else {
    *context = context_pair->second;
    if (context->client_connection_id != new_client_connection_id) {
      QUIC_LOG(ERROR)
          << "Received incorrect context registration for existing flow_id "
          << new_flow_id << " mismatched client "
          << context->client_connection_id << " " << new_client_connection_id;
      return false;
    }
    if (context->server_connection_id != new_server_connection_id) {
      QUIC_LOG(ERROR)
          << "Received incorrect context registration for existing flow_id "
          << new_flow_id << " mismatched server "
          << context->server_connection_id << " " << new_server_connection_id;
      return false;
    }
    if (context->server_address != new_server_address) {
      QUIC_LOG(ERROR)
          << "Received incorrect context registration for existing flow_id "
          << new_flow_id << " mismatched server " << context->server_address
          << " " << new_server_address;
      return false;
    }
    if (!context->validated) {
      context->validated = true;
      contexts_[new_flow_id] = *context;
      QUIC_DLOG(INFO) << "Successfully validated remotely-unvalidated flow_id "
                      << new_flow_id << " to " << new_server_address
                      << " client " << new_client_connection_id << " server "
                      << new_server_connection_id;
    } else {
      QUIC_DVLOG(1) << "Decompressing using incoming locally-validated "
                       "remotely-unvalidated flow_id "
                    << new_flow_id << " to " << new_server_address << " client "
                    << new_client_connection_id << " server "
                    << new_server_connection_id;
    }
  }
  return true;
}

bool MasqueCompressionEngine::WriteDecompressedPacket(
    QuicDataReader* reader,
    const MasqueCompressionContext& context,
    std::vector<char>* packet,
    bool* version_present) {
  QuicConnectionId destination_connection_id, source_connection_id;
  if (masque_session_->perspective() == Perspective::IS_SERVER) {
    destination_connection_id = context.server_connection_id;
    source_connection_id = context.client_connection_id;
  } else {
    destination_connection_id = context.client_connection_id;
    source_connection_id = context.server_connection_id;
  }

  size_t packet_length =
      reader->BytesRemaining() + destination_connection_id.length();
  uint8_t first_byte;
  if (!reader->ReadUInt8(&first_byte)) {
    QUIC_DLOG(ERROR) << "Failed to read first_byte";
    return false;
  }
  *version_present = (first_byte & FLAGS_LONG_HEADER) != 0;
  if (*version_present) {
    packet_length += sizeof(uint8_t) * 2 + source_connection_id.length();
  }
  *packet = std::vector<char>(packet_length);
  QuicDataWriter writer(packet->size(), packet->data());
  if (!writer.WriteUInt8(first_byte)) {
    QUIC_BUG << "Failed to write first_byte";
    return false;
  }
  if (*version_present) {
    QuicVersionLabel version_label;
    if (!reader->ReadUInt32(&version_label)) {
      QUIC_DLOG(ERROR) << "Failed to read version";
      return false;
    }
    if (!writer.WriteUInt32(version_label)) {
      QUIC_BUG << "Failed to write version";
      return false;
    }
    if (!writer.WriteLengthPrefixedConnectionId(destination_connection_id)) {
      QUIC_BUG << "Failed to write long header destination_connection_id";
      return false;
    }
    if (!writer.WriteLengthPrefixedConnectionId(source_connection_id)) {
      QUIC_BUG << "Failed to write long header source_connection_id";
      return false;
    }
  } else {
    if (!writer.WriteConnectionId(destination_connection_id)) {
      QUIC_BUG << "Failed to write short header destination_connection_id";
      return false;
    }
  }
  quiche::QuicheStringPiece payload = reader->ReadRemainingPayload();
  if (!writer.WriteStringPiece(payload)) {
    QUIC_BUG << "Failed to write payload";
    return false;
  }
  return true;
}

bool MasqueCompressionEngine::DecompressDatagram(
    quiche::QuicheStringPiece datagram,
    QuicConnectionId* client_connection_id,
    QuicConnectionId* server_connection_id,
    QuicSocketAddress* server_address,
    std::vector<char>* packet,
    bool* version_present) {
  QUIC_DVLOG(1) << "Decompressing DATAGRAM frame of length "
                << datagram.length();
  QuicDataReader reader(datagram);
  QuicDatagramFlowId flow_id;
  if (!reader.ReadVarInt62(&flow_id)) {
    QUIC_DLOG(ERROR) << "Could not read flow_id";
    return false;
  }
  MasqueCompressionContext context;
  if (flow_id == kFlowId0) {
    if (!ParseCompressionContext(&reader, &context)) {
      return false;
    }
  } else {
    auto context_pair = contexts_.find(flow_id);
    if (context_pair == contexts_.end()) {
      QUIC_DLOG(ERROR) << "Received unknown flow_id " << flow_id;
      return false;
    }
    context = context_pair->second;

    if (!context.validated) {
      context.validated = true;
      contexts_[flow_id] = context;
      QUIC_DLOG(INFO) << "Successfully validated remotely-validated flow_id "
                      << flow_id << " to " << context.server_address
                      << " client " << context.client_connection_id
                      << " server " << context.server_connection_id;
    } else {
      QUIC_DVLOG(1) << "Decompressing using incoming locally-validated "
                       "remotely-validated flow_id "
                    << flow_id << " to " << context.server_address << " client "
                    << context.client_connection_id << " server "
                    << context.server_connection_id;
    }
  }

  if (!WriteDecompressedPacket(&reader, context, packet, version_present)) {
    return false;
  }

  *server_address = context.server_address;
  *client_connection_id = context.client_connection_id;
  *server_connection_id = context.server_connection_id;

  QUIC_DVLOG(2) << "Decompressed client " << context.client_connection_id
                << " server " << context.server_connection_id << "\n"
                << quiche::QuicheTextUtils::HexDump(quiche::QuicheStringPiece(
                       packet->data(), packet->size()));

  return true;
}

QuicDatagramFlowId MasqueCompressionEngine::GetNextFlowId() {
  const QuicDatagramFlowId next_flow_id = next_flow_id_;
  next_flow_id_ += 2;
  return next_flow_id;
}

void MasqueCompressionEngine::UnregisterClientConnectionId(
    QuicConnectionId client_connection_id) {
  std::vector<QuicDatagramFlowId> flow_ids_to_remove;
  for (const auto& kv : contexts_) {
    const MasqueCompressionContext& context = kv.second;
    if (context.client_connection_id == client_connection_id) {
      flow_ids_to_remove.push_back(kv.first);
    }
  }
  for (QuicDatagramFlowId flow_id : flow_ids_to_remove) {
    contexts_.erase(flow_id);
  }
}

}  // namespace quic
