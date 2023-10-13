// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_CAPSULE_H_
#define QUICHE_COMMON_CAPSULE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_ip_address.h"
#include "quiche/web_transport/web_transport.h"

namespace quiche {

enum class CapsuleType : uint64_t {
  // Casing in this enum matches the IETF specifications.
  DATAGRAM = 0x00,             // RFC 9297.
  LEGACY_DATAGRAM = 0xff37a0,  // draft-ietf-masque-h3-datagram-04.
  LEGACY_DATAGRAM_WITHOUT_CONTEXT =
      0xff37a5,  // draft-ietf-masque-h3-datagram-05 to -08.

  // <https://datatracker.ietf.org/doc/draft-ietf-webtrans-http3/>
  CLOSE_WEBTRANSPORT_SESSION = 0x2843,
  DRAIN_WEBTRANSPORT_SESSION = 0x78ae,

  // draft-ietf-masque-connect-ip-03.
  ADDRESS_ASSIGN = 0x1ECA6A00,
  ADDRESS_REQUEST = 0x1ECA6A01,
  ROUTE_ADVERTISEMENT = 0x1ECA6A02,

  // <https://ietf-wg-webtrans.github.io/draft-webtransport-http2/draft-ietf-webtrans-http2.html#name-webtransport-capsules>
  WT_RESET_STREAM = 0x190b4d39,
  WT_STOP_SENDING = 0x190b4d3a,
  WT_STREAM = 0x190b4d3b,
  WT_STREAM_WITH_FIN = 0x190b4d3c,
  // Should be removed as a result of
  // <https://github.com/ietf-wg-webtrans/draft-webtransport-http2/issues/27>.
  // WT_MAX_DATA = 0x190b4d3d,
  WT_MAX_STREAM_DATA = 0x190b4d3e,
  WT_MAX_STREAMS_BIDI = 0x190b4d3f,
  WT_MAX_STREAMS_UNIDI = 0x190b4d40,

  // TODO(b/264263113): implement those.
  // PADDING = 0x190b4d38,
  // WT_DATA_BLOCKED = 0x190b4d41,
  // WT_STREAM_DATA_BLOCKED = 0x190b4d42,
  // WT_STREAMS_BLOCKED_BIDI = 0x190b4d43,
  // WT_STREAMS_BLOCKED_UNIDI = 0x190b4d44,
};

QUICHE_EXPORT std::string CapsuleTypeToString(CapsuleType capsule_type);
QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const CapsuleType& capsule_type);

// General.
struct QUICHE_EXPORT DatagramCapsule {
  absl::string_view http_datagram_payload;

  std::string ToString() const;
  CapsuleType capsule_type() const { return CapsuleType::DATAGRAM; }
  bool operator==(const DatagramCapsule& other) const {
    return http_datagram_payload == other.http_datagram_payload;
  }
};

struct QUICHE_EXPORT LegacyDatagramCapsule {
  absl::string_view http_datagram_payload;

  std::string ToString() const;
  CapsuleType capsule_type() const { return CapsuleType::LEGACY_DATAGRAM; }
  bool operator==(const LegacyDatagramCapsule& other) const {
    return http_datagram_payload == other.http_datagram_payload;
  }
};

struct QUICHE_EXPORT LegacyDatagramWithoutContextCapsule {
  absl::string_view http_datagram_payload;

  std::string ToString() const;
  CapsuleType capsule_type() const {
    return CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT;
  }
  bool operator==(const LegacyDatagramWithoutContextCapsule& other) const {
    return http_datagram_payload == other.http_datagram_payload;
  }
};

// WebTransport over HTTP/3.
struct QUICHE_EXPORT CloseWebTransportSessionCapsule {
  webtransport::SessionErrorCode error_code;
  absl::string_view error_message;

  std::string ToString() const;
  CapsuleType capsule_type() const {
    return CapsuleType::CLOSE_WEBTRANSPORT_SESSION;
  }
  bool operator==(const CloseWebTransportSessionCapsule& other) const {
    return error_code == other.error_code &&
           error_message == other.error_message;
  }
};
struct QUICHE_EXPORT DrainWebTransportSessionCapsule {
  std::string ToString() const;
  CapsuleType capsule_type() const {
    return CapsuleType::DRAIN_WEBTRANSPORT_SESSION;
  }
  bool operator==(const DrainWebTransportSessionCapsule&) const { return true; }
};

// MASQUE CONNECT-IP.
struct QUICHE_EXPORT PrefixWithId {
  uint64_t request_id;
  quiche::QuicheIpPrefix ip_prefix;
  bool operator==(const PrefixWithId& other) const;
};
struct QUICHE_EXPORT IpAddressRange {
  quiche::QuicheIpAddress start_ip_address;
  quiche::QuicheIpAddress end_ip_address;
  uint8_t ip_protocol;
  bool operator==(const IpAddressRange& other) const;
};

struct QUICHE_EXPORT AddressAssignCapsule {
  std::vector<PrefixWithId> assigned_addresses;
  bool operator==(const AddressAssignCapsule& other) const;
  std::string ToString() const;
  CapsuleType capsule_type() const { return CapsuleType::ADDRESS_ASSIGN; }
};
struct QUICHE_EXPORT AddressRequestCapsule {
  std::vector<PrefixWithId> requested_addresses;
  bool operator==(const AddressRequestCapsule& other) const;
  std::string ToString() const;
  CapsuleType capsule_type() const { return CapsuleType::ADDRESS_REQUEST; }
};
struct QUICHE_EXPORT RouteAdvertisementCapsule {
  std::vector<IpAddressRange> ip_address_ranges;
  bool operator==(const RouteAdvertisementCapsule& other) const;
  std::string ToString() const;
  CapsuleType capsule_type() const { return CapsuleType::ROUTE_ADVERTISEMENT; }
};
struct QUICHE_EXPORT UnknownCapsule {
  uint64_t type;
  absl::string_view payload;

  std::string ToString() const;
  CapsuleType capsule_type() const { return static_cast<CapsuleType>(type); }
  bool operator==(const UnknownCapsule& other) const {
    return type == other.type && payload == other.payload;
  }
};

// WebTransport over HTTP/2.
struct QUICHE_EXPORT WebTransportStreamDataCapsule {
  webtransport::StreamId stream_id;
  absl::string_view data;
  bool fin;

  bool operator==(const WebTransportStreamDataCapsule& other) const;
  std::string ToString() const;
  CapsuleType capsule_type() const {
    return fin ? CapsuleType::WT_STREAM_WITH_FIN : CapsuleType::WT_STREAM;
  }
};
struct QUICHE_EXPORT WebTransportResetStreamCapsule {
  webtransport::StreamId stream_id;
  uint64_t error_code;

  bool operator==(const WebTransportResetStreamCapsule& other) const;
  std::string ToString() const;
  CapsuleType capsule_type() const { return CapsuleType::WT_RESET_STREAM; }
};
struct QUICHE_EXPORT WebTransportStopSendingCapsule {
  webtransport::StreamId stream_id;
  uint64_t error_code;

  bool operator==(const WebTransportStopSendingCapsule& other) const;
  std::string ToString() const;
  CapsuleType capsule_type() const { return CapsuleType::WT_STOP_SENDING; }
};
struct QUICHE_EXPORT WebTransportMaxStreamDataCapsule {
  webtransport::StreamId stream_id;
  uint64_t max_stream_data;

  bool operator==(const WebTransportMaxStreamDataCapsule& other) const;
  std::string ToString() const;
  CapsuleType capsule_type() const { return CapsuleType::WT_MAX_STREAM_DATA; }
};
struct QUICHE_EXPORT WebTransportMaxStreamsCapsule {
  webtransport::StreamType stream_type;
  uint64_t max_stream_count;

  bool operator==(const WebTransportMaxStreamsCapsule& other) const;
  std::string ToString() const;
  CapsuleType capsule_type() const {
    return stream_type == webtransport::StreamType::kBidirectional
               ? CapsuleType::WT_MAX_STREAMS_BIDI
               : CapsuleType::WT_MAX_STREAMS_UNIDI;
  }
};

// Capsule from RFC 9297.
// IMPORTANT NOTE: Capsule does not own any of the absl::string_view memory it
// points to. Strings saved into a capsule must outlive the capsule object. Any
// code that sees a capsule in a callback needs to either process it immediately
// or perform its own deep copy.
class QUICHE_EXPORT Capsule {
 public:
  static Capsule Datagram(
      absl::string_view http_datagram_payload = absl::string_view());
  static Capsule LegacyDatagram(
      absl::string_view http_datagram_payload = absl::string_view());
  static Capsule LegacyDatagramWithoutContext(
      absl::string_view http_datagram_payload = absl::string_view());
  static Capsule CloseWebTransportSession(
      webtransport::SessionErrorCode error_code = 0,
      absl::string_view error_message = "");
  static Capsule AddressRequest();
  static Capsule AddressAssign();
  static Capsule RouteAdvertisement();
  static Capsule Unknown(
      uint64_t capsule_type,
      absl::string_view unknown_capsule_data = absl::string_view());

  template <typename CapsuleStruct>
  explicit Capsule(CapsuleStruct capsule) : capsule_(std::move(capsule)) {}
  bool operator==(const Capsule& other) const;

  // Human-readable information string for debugging purposes.
  std::string ToString() const;
  friend QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                                const Capsule& capsule);

  CapsuleType capsule_type() const {
    return absl::visit(
        [](const auto& capsule) { return capsule.capsule_type(); }, capsule_);
  }
  DatagramCapsule& datagram_capsule() {
    return absl::get<DatagramCapsule>(capsule_);
  }
  const DatagramCapsule& datagram_capsule() const {
    return absl::get<DatagramCapsule>(capsule_);
  }
  LegacyDatagramCapsule& legacy_datagram_capsule() {
    return absl::get<LegacyDatagramCapsule>(capsule_);
  }
  const LegacyDatagramCapsule& legacy_datagram_capsule() const {
    return absl::get<LegacyDatagramCapsule>(capsule_);
  }
  LegacyDatagramWithoutContextCapsule&
  legacy_datagram_without_context_capsule() {
    return absl::get<LegacyDatagramWithoutContextCapsule>(capsule_);
  }
  const LegacyDatagramWithoutContextCapsule&
  legacy_datagram_without_context_capsule() const {
    return absl::get<LegacyDatagramWithoutContextCapsule>(capsule_);
  }
  CloseWebTransportSessionCapsule& close_web_transport_session_capsule() {
    return absl::get<CloseWebTransportSessionCapsule>(capsule_);
  }
  const CloseWebTransportSessionCapsule& close_web_transport_session_capsule()
      const {
    return absl::get<CloseWebTransportSessionCapsule>(capsule_);
  }
  AddressRequestCapsule& address_request_capsule() {
    return absl::get<AddressRequestCapsule>(capsule_);
  }
  const AddressRequestCapsule& address_request_capsule() const {
    return absl::get<AddressRequestCapsule>(capsule_);
  }
  AddressAssignCapsule& address_assign_capsule() {
    return absl::get<AddressAssignCapsule>(capsule_);
  }
  const AddressAssignCapsule& address_assign_capsule() const {
    return absl::get<AddressAssignCapsule>(capsule_);
  }
  RouteAdvertisementCapsule& route_advertisement_capsule() {
    return absl::get<RouteAdvertisementCapsule>(capsule_);
  }
  const RouteAdvertisementCapsule& route_advertisement_capsule() const {
    return absl::get<RouteAdvertisementCapsule>(capsule_);
  }
  WebTransportStreamDataCapsule& web_transport_stream_data() {
    return absl::get<WebTransportStreamDataCapsule>(capsule_);
  }
  const WebTransportStreamDataCapsule& web_transport_stream_data() const {
    return absl::get<WebTransportStreamDataCapsule>(capsule_);
  }
  WebTransportResetStreamCapsule& web_transport_reset_stream() {
    return absl::get<WebTransportResetStreamCapsule>(capsule_);
  }
  const WebTransportResetStreamCapsule& web_transport_reset_stream() const {
    return absl::get<WebTransportResetStreamCapsule>(capsule_);
  }
  WebTransportStopSendingCapsule& web_transport_stop_sending() {
    return absl::get<WebTransportStopSendingCapsule>(capsule_);
  }
  const WebTransportStopSendingCapsule& web_transport_stop_sending() const {
    return absl::get<WebTransportStopSendingCapsule>(capsule_);
  }
  WebTransportMaxStreamDataCapsule& web_transport_max_stream_data() {
    return absl::get<WebTransportMaxStreamDataCapsule>(capsule_);
  }
  const WebTransportMaxStreamDataCapsule& web_transport_max_stream_data()
      const {
    return absl::get<WebTransportMaxStreamDataCapsule>(capsule_);
  }
  WebTransportMaxStreamsCapsule& web_transport_max_streams() {
    return absl::get<WebTransportMaxStreamsCapsule>(capsule_);
  }
  const WebTransportMaxStreamsCapsule& web_transport_max_streams() const {
    return absl::get<WebTransportMaxStreamsCapsule>(capsule_);
  }
  UnknownCapsule& unknown_capsule() {
    return absl::get<UnknownCapsule>(capsule_);
  }
  const UnknownCapsule& unknown_capsule() const {
    return absl::get<UnknownCapsule>(capsule_);
  }

 private:
  absl::variant<DatagramCapsule, LegacyDatagramCapsule,
                LegacyDatagramWithoutContextCapsule,
                CloseWebTransportSessionCapsule,
                DrainWebTransportSessionCapsule, AddressRequestCapsule,
                AddressAssignCapsule, RouteAdvertisementCapsule,
                WebTransportStreamDataCapsule, WebTransportResetStreamCapsule,
                WebTransportStopSendingCapsule, WebTransportMaxStreamsCapsule,
                WebTransportMaxStreamDataCapsule, UnknownCapsule>
      capsule_;
};

namespace test {
class CapsuleParserPeer;
}  // namespace test

class QUICHE_EXPORT CapsuleParser {
 public:
  class QUICHE_EXPORT Visitor {
   public:
    virtual ~Visitor() {}

    // Called when a capsule has been successfully parsed. The return value
    // indicates whether the contents of the capsule are valid: if false is
    // returned, the parse operation will be considered failed and
    // OnCapsuleParseFailure will be called. Note that since Capsule does not
    // own the memory backing its string_views, that memory is only valid until
    // this callback returns. Visitors that wish to access the capsule later
    // MUST make a deep copy before this returns.
    virtual bool OnCapsule(const Capsule& capsule) = 0;

    virtual void OnCapsuleParseFailure(absl::string_view error_message) = 0;
  };

  // |visitor| must be non-null, and must outlive CapsuleParser.
  explicit CapsuleParser(Visitor* visitor);

  // Ingests a capsule fragment (any fragment of bytes from the capsule data
  // stream) and parses and complete capsules it encounters. Returns false if a
  // parsing error occurred.
  bool IngestCapsuleFragment(absl::string_view capsule_fragment);

  void ErrorIfThereIsRemainingBufferedData();

  friend class test::CapsuleParserPeer;

 private:
  // Attempts to parse a single capsule from |buffered_data_|. If a full capsule
  // is not available, returns 0. If a parsing error occurs, returns an error.
  // Otherwise, returns the number of bytes in the parsed capsule.
  absl::StatusOr<size_t> AttemptParseCapsule();
  void ReportParseFailure(absl::string_view error_message);

  // Whether a parsing error has occurred.
  bool parsing_error_occurred_ = false;
  // Visitor which will receive callbacks, unowned.
  Visitor* visitor_;

  std::string buffered_data_;
};

// Serializes |capsule| into a newly allocated buffer.
QUICHE_EXPORT quiche::QuicheBuffer SerializeCapsule(
    const Capsule& capsule, quiche::QuicheBufferAllocator* allocator);

}  // namespace quiche

#endif  // QUICHE_COMMON_CAPSULE_H_
