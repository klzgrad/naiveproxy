// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/masque/connect_ip_datagram_payload.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"

namespace quiche {

// static
std::unique_ptr<ConnectIpDatagramPayload> ConnectIpDatagramPayload::Parse(
    absl::string_view datagram_payload) {
  QuicheDataReader data_reader(datagram_payload);

  uint64_t context_id;
  if (!data_reader.ReadVarInt62(&context_id)) {
    QUICHE_DVLOG(1) << "Could not parse malformed IP proxy payload";
    return nullptr;
  }

  if (ContextId{context_id} == ConnectIpDatagramIpPacketPayload::kContextId) {
    return std::make_unique<ConnectIpDatagramIpPacketPayload>(
        data_reader.ReadRemainingPayload());
  } else {
    return std::make_unique<ConnectIpDatagramUnknownPayload>(
        ContextId{context_id}, data_reader.ReadRemainingPayload());
  }
}

std::string ConnectIpDatagramPayload::Serialize() const {
  std::string buffer(SerializedLength(), '\0');
  QuicheDataWriter writer(buffer.size(), buffer.data());

  bool result = SerializeTo(writer);
  QUICHE_DCHECK(result);
  QUICHE_DCHECK_EQ(writer.remaining(), 0u);

  return buffer;
}

ConnectIpDatagramIpPacketPayload::ConnectIpDatagramIpPacketPayload(
    absl::string_view ip_packet)
    : ip_packet_(ip_packet) {}

ConnectIpDatagramPayload::ContextId
ConnectIpDatagramIpPacketPayload::GetContextId() const {
  return kContextId;
}

ConnectIpDatagramPayload::Type ConnectIpDatagramIpPacketPayload::GetType()
    const {
  return Type::kIpPacket;
}

absl::string_view ConnectIpDatagramIpPacketPayload::GetIpProxyingPayload()
    const {
  return ip_packet_;
}

size_t ConnectIpDatagramIpPacketPayload::SerializedLength() const {
  return ip_packet_.size() +
         QuicheDataWriter::GetVarInt62Len(uint64_t{kContextId});
}

bool ConnectIpDatagramIpPacketPayload::SerializeTo(
    QuicheDataWriter& writer) const {
  if (!writer.WriteVarInt62(uint64_t{kContextId})) {
    return false;
  }

  if (!writer.WriteStringPiece(ip_packet_)) {
    return false;
  }

  return true;
}

ConnectIpDatagramUnknownPayload::ConnectIpDatagramUnknownPayload(
    ContextId context_id, absl::string_view ip_proxying_payload)
    : context_id_(context_id), ip_proxying_payload_(ip_proxying_payload) {
  if (context_id == ConnectIpDatagramIpPacketPayload::kContextId) {
    QUICHE_BUG(ip_proxy_unknown_payload_ip_context)
        << "ConnectIpDatagramUnknownPayload created with IP packet context "
           "ID (0). Should instead create a "
           "ConnectIpDatagramIpPacketPayload.";
  }
}

ConnectIpDatagramPayload::ContextId
ConnectIpDatagramUnknownPayload::GetContextId() const {
  return context_id_;
}

ConnectIpDatagramPayload::Type ConnectIpDatagramUnknownPayload::GetType()
    const {
  return Type::kUnknown;
}
absl::string_view ConnectIpDatagramUnknownPayload::GetIpProxyingPayload()
    const {
  return ip_proxying_payload_;
}

size_t ConnectIpDatagramUnknownPayload::SerializedLength() const {
  return ip_proxying_payload_.size() +
         QuicheDataWriter::GetVarInt62Len(uint64_t{context_id_});
}

bool ConnectIpDatagramUnknownPayload::SerializeTo(
    QuicheDataWriter& writer) const {
  if (!writer.WriteVarInt62(uint64_t{context_id_})) {
    return false;
  }

  if (!writer.WriteStringPiece(ip_proxying_payload_)) {
    return false;
  }

  return true;
}

}  // namespace quiche
