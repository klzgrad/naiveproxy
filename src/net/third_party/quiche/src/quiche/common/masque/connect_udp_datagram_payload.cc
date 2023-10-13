// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/masque/connect_udp_datagram_payload.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"

namespace quiche {

// static
std::unique_ptr<ConnectUdpDatagramPayload> ConnectUdpDatagramPayload::Parse(
    absl::string_view datagram_payload) {
  QuicheDataReader data_reader(datagram_payload);

  uint64_t context_id;
  if (!data_reader.ReadVarInt62(&context_id)) {
    QUICHE_DVLOG(1) << "Could not parse malformed UDP proxy payload";
    return nullptr;
  }

  if (ContextId{context_id} == ConnectUdpDatagramUdpPacketPayload::kContextId) {
    return std::make_unique<ConnectUdpDatagramUdpPacketPayload>(
        data_reader.ReadRemainingPayload());
  } else {
    return std::make_unique<ConnectUdpDatagramUnknownPayload>(
        ContextId{context_id}, data_reader.ReadRemainingPayload());
  }
}

std::string ConnectUdpDatagramPayload::Serialize() const {
  std::string buffer(SerializedLength(), '\0');
  QuicheDataWriter writer(buffer.size(), buffer.data());

  bool result = SerializeTo(writer);
  QUICHE_DCHECK(result);
  QUICHE_DCHECK_EQ(writer.remaining(), 0u);

  return buffer;
}

ConnectUdpDatagramUdpPacketPayload::ConnectUdpDatagramUdpPacketPayload(
    absl::string_view udp_packet)
    : udp_packet_(udp_packet) {}

ConnectUdpDatagramPayload::ContextId
ConnectUdpDatagramUdpPacketPayload::GetContextId() const {
  return kContextId;
}

ConnectUdpDatagramPayload::Type ConnectUdpDatagramUdpPacketPayload::GetType()
    const {
  return Type::kUdpPacket;
}

absl::string_view ConnectUdpDatagramUdpPacketPayload::GetUdpProxyingPayload()
    const {
  return udp_packet_;
}

size_t ConnectUdpDatagramUdpPacketPayload::SerializedLength() const {
  return udp_packet_.size() +
         QuicheDataWriter::GetVarInt62Len(uint64_t{kContextId});
}

bool ConnectUdpDatagramUdpPacketPayload::SerializeTo(
    QuicheDataWriter& writer) const {
  if (!writer.WriteVarInt62(uint64_t{kContextId})) {
    return false;
  }

  if (!writer.WriteStringPiece(udp_packet_)) {
    return false;
  }

  return true;
}

ConnectUdpDatagramUnknownPayload::ConnectUdpDatagramUnknownPayload(
    ContextId context_id, absl::string_view udp_proxying_payload)
    : context_id_(context_id), udp_proxying_payload_(udp_proxying_payload) {
  if (context_id == ConnectUdpDatagramUdpPacketPayload::kContextId) {
    QUICHE_BUG(udp_proxy_unknown_payload_udp_context)
        << "ConnectUdpDatagramUnknownPayload created with UDP packet context "
           "type (0). Should instead create a "
           "ConnectUdpDatagramUdpPacketPayload.";
  }
}

ConnectUdpDatagramPayload::ContextId
ConnectUdpDatagramUnknownPayload::GetContextId() const {
  return context_id_;
}

ConnectUdpDatagramPayload::Type ConnectUdpDatagramUnknownPayload::GetType()
    const {
  return Type::kUnknown;
}
absl::string_view ConnectUdpDatagramUnknownPayload::GetUdpProxyingPayload()
    const {
  return udp_proxying_payload_;
}

size_t ConnectUdpDatagramUnknownPayload::SerializedLength() const {
  return udp_proxying_payload_.size() +
         QuicheDataWriter::GetVarInt62Len(uint64_t{context_id_});
}

bool ConnectUdpDatagramUnknownPayload::SerializeTo(
    QuicheDataWriter& writer) const {
  if (!writer.WriteVarInt62(uint64_t{context_id_})) {
    return false;
  }

  if (!writer.WriteStringPiece(udp_proxying_payload_)) {
    return false;
  }

  return true;
}

}  // namespace quiche
