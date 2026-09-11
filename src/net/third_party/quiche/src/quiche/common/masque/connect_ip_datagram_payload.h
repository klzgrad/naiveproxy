// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_MASQUE_CONNECT_IP_DATAGRAM_PAYLOAD_H_
#define QUICHE_COMMON_MASQUE_CONNECT_IP_DATAGRAM_PAYLOAD_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_data_writer.h"

namespace quiche {

// IP-proxying HTTP Datagram payload for use with CONNECT-IP. See RFC 9484,
// Section 6.
class QUICHE_EXPORT ConnectIpDatagramPayload {
 public:
  using ContextId = uint64_t;
  enum class Type { kIpPacket, kUnknown };

  // Parse from `datagram_payload` (a wire-format IP-proxying HTTP datagram
  // payload). Returns nullptr on error. The created ConnectIpDatagramPayload
  // object may use absl::string_views pointing into `datagram_payload`, so the
  // data pointed to by `datagram_payload` must outlive the created
  // ConnectIpDatagramPayload object.
  static std::unique_ptr<ConnectIpDatagramPayload> Parse(
      absl::string_view datagram_payload);

  ConnectIpDatagramPayload() = default;

  ConnectIpDatagramPayload(const ConnectIpDatagramPayload&) = delete;
  ConnectIpDatagramPayload& operator=(const ConnectIpDatagramPayload&) = delete;

  virtual ~ConnectIpDatagramPayload() = default;

  virtual ContextId GetContextId() const = 0;
  virtual Type GetType() const = 0;
  // Get the inner payload (the IP Proxying Payload).
  virtual absl::string_view GetIpProxyingPayload() const = 0;

  // Length of this IP-proxying HTTP datagram payload in wire format.
  virtual size_t SerializedLength() const = 0;
  // Write a wire-format buffer for the payload. Returns false on write failure
  // (typically due to `writer` buffer being full).
  virtual bool SerializeTo(QuicheDataWriter& writer) const = 0;

  // Write a wire-format buffer.
  std::string Serialize() const;
};

// IP-proxying HTTP Datagram payload that encodes an IP packet.
class QUICHE_EXPORT ConnectIpDatagramIpPacketPayload final
    : public ConnectIpDatagramPayload {
 public:
  static constexpr ContextId kContextId = 0;

  // The string pointed to by `ip_packet` must outlive the created
  // ConnectIpDatagramIpPacketPayload.
  explicit ConnectIpDatagramIpPacketPayload(absl::string_view ip_packet);

  ContextId GetContextId() const override;
  Type GetType() const override;
  absl::string_view GetIpProxyingPayload() const override;
  size_t SerializedLength() const override;
  bool SerializeTo(QuicheDataWriter& writer) const override;

  absl::string_view ip_packet() const { return ip_packet_; }

 private:
  absl::string_view ip_packet_;
};

class QUICHE_EXPORT ConnectIpDatagramUnknownPayload final
    : public ConnectIpDatagramPayload {
 public:
  // `ip_proxying_payload` represents the inner payload contained by the IP-
  // proxying HTTP datagram payload. The string pointed to by `inner_payload`
  // must outlive the created ConnectIpDatagramUnknownPayload.
  ConnectIpDatagramUnknownPayload(ContextId context_id,
                                  absl::string_view ip_proxying_payload);

  ContextId GetContextId() const override;
  Type GetType() const override;
  absl::string_view GetIpProxyingPayload() const override;
  size_t SerializedLength() const override;
  bool SerializeTo(QuicheDataWriter& writer) const override;

 private:
  ContextId context_id_;
  absl::string_view ip_proxying_payload_;  // The inner payload.
};

}  // namespace quiche

#endif  // QUICHE_COMMON_MASQUE_CONNECT_IP_DATAGRAM_PAYLOAD_H_
