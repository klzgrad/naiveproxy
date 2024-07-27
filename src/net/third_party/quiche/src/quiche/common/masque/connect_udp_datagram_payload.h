// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_MASQUE_CONNECT_UDP_DATAGRAM_PAYLOAD_H_
#define QUICHE_COMMON_MASQUE_CONNECT_UDP_DATAGRAM_PAYLOAD_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/common/quiche_data_writer.h"

namespace quiche {

// UDP-proxying HTTP Datagram payload for use with CONNECT-UDP. See RFC 9298,
// Section 5.
class QUICHE_EXPORT ConnectUdpDatagramPayload {
 public:
  using ContextId = uint64_t;
  enum class Type { kUdpPacket, kUnknown };

  // Parse from `datagram_payload` (a wire-format UDP-proxying HTTP datagram
  // payload). Returns nullptr on error. The created ConnectUdpDatagramPayload
  // object may use absl::string_views pointing into `datagram_payload`, so the
  // data pointed to by `datagram_payload` must outlive the created
  // ConnectUdpDatagramPayload object.
  static std::unique_ptr<ConnectUdpDatagramPayload> Parse(
      absl::string_view datagram_payload);

  ConnectUdpDatagramPayload() = default;

  ConnectUdpDatagramPayload(const ConnectUdpDatagramPayload&) = delete;
  ConnectUdpDatagramPayload& operator=(const ConnectUdpDatagramPayload&) =
      delete;

  virtual ~ConnectUdpDatagramPayload() = default;

  virtual ContextId GetContextId() const = 0;
  virtual Type GetType() const = 0;
  // Get the inner payload (the UDP Proxying Payload).
  virtual absl::string_view GetUdpProxyingPayload() const = 0;

  // Length of this UDP-proxying HTTP datagram payload in wire format.
  virtual size_t SerializedLength() const = 0;
  // Write a wire-format buffer for the payload. Returns false on write failure
  // (typically due to `writer` buffer being full).
  virtual bool SerializeTo(QuicheDataWriter& writer) const = 0;

  // Write a wire-format buffer.
  std::string Serialize() const;
};

// UDP-proxying HTTP Datagram payload that encodes a UDP packet.
class QUICHE_EXPORT ConnectUdpDatagramUdpPacketPayload final
    : public ConnectUdpDatagramPayload {
 public:
  static constexpr ContextId kContextId = 0;

  // The string pointed to by `udp_packet` must outlive the created
  // ConnectUdpDatagramUdpPacketPayload.
  explicit ConnectUdpDatagramUdpPacketPayload(absl::string_view udp_packet);

  ContextId GetContextId() const override;
  Type GetType() const override;
  absl::string_view GetUdpProxyingPayload() const override;
  size_t SerializedLength() const override;
  bool SerializeTo(QuicheDataWriter& writer) const override;

  absl::string_view udp_packet() const { return udp_packet_; }

 private:
  absl::string_view udp_packet_;
};

class QUICHE_EXPORT ConnectUdpDatagramUnknownPayload final
    : public ConnectUdpDatagramPayload {
 public:
  // `udp_proxying_payload` represents the inner payload contained by the UDP-
  // proxying HTTP datagram payload. The string pointed to by `inner_payload`
  // must outlive the created ConnectUdpDatagramUnknownPayload.
  ConnectUdpDatagramUnknownPayload(ContextId context_id,
                                   absl::string_view udp_proxying_payload);

  ContextId GetContextId() const override;
  Type GetType() const override;
  absl::string_view GetUdpProxyingPayload() const override;
  size_t SerializedLength() const override;
  bool SerializeTo(QuicheDataWriter& writer) const override;

 private:
  ContextId context_id_;
  absl::string_view udp_proxying_payload_;  // The inner payload.
};

}  // namespace quiche

#endif  // QUICHE_COMMON_MASQUE_CONNECT_UDP_DATAGRAM_PAYLOAD_H_
