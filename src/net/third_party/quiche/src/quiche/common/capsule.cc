// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/capsule.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"
#include "quiche/common/quiche_ip_address.h"
#include "quiche/common/quiche_status_utils.h"
#include "quiche/common/wire_serialization.h"
#include "quiche/web_transport/web_transport.h"

namespace quiche {

std::string CapsuleTypeToString(CapsuleType capsule_type) {
  switch (capsule_type) {
    case CapsuleType::DATAGRAM:
      return "DATAGRAM";
    case CapsuleType::LEGACY_DATAGRAM:
      return "LEGACY_DATAGRAM";
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT:
      return "LEGACY_DATAGRAM_WITHOUT_CONTEXT";
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      return "CLOSE_WEBTRANSPORT_SESSION";
    case CapsuleType::DRAIN_WEBTRANSPORT_SESSION:
      return "DRAIN_WEBTRANSPORT_SESSION";
    case CapsuleType::ADDRESS_REQUEST:
      return "ADDRESS_REQUEST";
    case CapsuleType::ADDRESS_ASSIGN:
      return "ADDRESS_ASSIGN";
    case CapsuleType::ROUTE_ADVERTISEMENT:
      return "ROUTE_ADVERTISEMENT";
    case CapsuleType::WT_STREAM:
      return "WT_STREAM";
    case CapsuleType::WT_STREAM_WITH_FIN:
      return "WT_STREAM_WITH_FIN";
    case CapsuleType::WT_RESET_STREAM:
      return "WT_RESET_STREAM";
    case CapsuleType::WT_STOP_SENDING:
      return "WT_STOP_SENDING";
    case CapsuleType::WT_MAX_STREAM_DATA:
      return "WT_MAX_STREAM_DATA";
    case CapsuleType::WT_MAX_STREAMS_BIDI:
      return "WT_MAX_STREAMS_BIDI";
    case CapsuleType::WT_MAX_STREAMS_UNIDI:
      return "WT_MAX_STREAMS_UNIDI";
  }
  return absl::StrCat("Unknown(", static_cast<uint64_t>(capsule_type), ")");
}

std::ostream& operator<<(std::ostream& os, const CapsuleType& capsule_type) {
  os << CapsuleTypeToString(capsule_type);
  return os;
}

// static
Capsule Capsule::Datagram(absl::string_view http_datagram_payload) {
  return Capsule(DatagramCapsule{http_datagram_payload});
}

// static
Capsule Capsule::LegacyDatagram(absl::string_view http_datagram_payload) {
  return Capsule(LegacyDatagramCapsule{http_datagram_payload});
}

// static
Capsule Capsule::LegacyDatagramWithoutContext(
    absl::string_view http_datagram_payload) {
  return Capsule(LegacyDatagramWithoutContextCapsule{http_datagram_payload});
}

// static
Capsule Capsule::CloseWebTransportSession(
    webtransport::SessionErrorCode error_code,
    absl::string_view error_message) {
  return Capsule(CloseWebTransportSessionCapsule({error_code, error_message}));
}

// static
Capsule Capsule::AddressRequest() { return Capsule(AddressRequestCapsule()); }

// static
Capsule Capsule::AddressAssign() { return Capsule(AddressAssignCapsule()); }

// static
Capsule Capsule::RouteAdvertisement() {
  return Capsule(RouteAdvertisementCapsule());
}

// static
Capsule Capsule::Unknown(uint64_t capsule_type,
                         absl::string_view unknown_capsule_data) {
  return Capsule(UnknownCapsule{capsule_type, unknown_capsule_data});
}

bool Capsule::operator==(const Capsule& other) const {
  return capsule_ == other.capsule_;
}

std::string DatagramCapsule::ToString() const {
  return absl::StrCat("DATAGRAM[",
                      absl::BytesToHexString(http_datagram_payload), "]");
}

std::string LegacyDatagramCapsule::ToString() const {
  return absl::StrCat("LEGACY_DATAGRAM[",
                      absl::BytesToHexString(http_datagram_payload), "]");
}

std::string LegacyDatagramWithoutContextCapsule::ToString() const {
  return absl::StrCat("LEGACY_DATAGRAM_WITHOUT_CONTEXT[",
                      absl::BytesToHexString(http_datagram_payload), "]");
}

std::string CloseWebTransportSessionCapsule::ToString() const {
  return absl::StrCat("CLOSE_WEBTRANSPORT_SESSION(error_code=", error_code,
                      ",error_message=\"", error_message, "\")");
}

std::string DrainWebTransportSessionCapsule::ToString() const {
  return "DRAIN_WEBTRANSPORT_SESSION()";
}

std::string AddressRequestCapsule::ToString() const {
  std::string rv = "ADDRESS_REQUEST[";
  for (auto requested_address : requested_addresses) {
    absl::StrAppend(&rv, "(", requested_address.request_id, "-",
                    requested_address.ip_prefix.ToString(), ")");
  }
  absl::StrAppend(&rv, "]");
  return rv;
}

std::string AddressAssignCapsule::ToString() const {
  std::string rv = "ADDRESS_ASSIGN[";
  for (auto assigned_address : assigned_addresses) {
    absl::StrAppend(&rv, "(", assigned_address.request_id, "-",
                    assigned_address.ip_prefix.ToString(), ")");
  }
  absl::StrAppend(&rv, "]");
  return rv;
}

std::string RouteAdvertisementCapsule::ToString() const {
  std::string rv = "ROUTE_ADVERTISEMENT[";
  for (auto ip_address_range : ip_address_ranges) {
    absl::StrAppend(&rv, "(", ip_address_range.start_ip_address.ToString(), "-",
                    ip_address_range.end_ip_address.ToString(), "-",
                    static_cast<int>(ip_address_range.ip_protocol), ")");
  }
  absl::StrAppend(&rv, "]");
  return rv;
}

std::string UnknownCapsule::ToString() const {
  return absl::StrCat("Unknown(", type, ") [", absl::BytesToHexString(payload),
                      "]");
}

std::string WebTransportStreamDataCapsule::ToString() const {
  return absl::StrCat(CapsuleTypeToString(capsule_type()),
                      " [stream_id=", stream_id,
                      ", data=", absl::BytesToHexString(data), "]");
}

std::string WebTransportResetStreamCapsule::ToString() const {
  return absl::StrCat("WT_RESET_STREAM(stream_id=", stream_id,
                      ", error_code=", error_code, ")");
}

std::string WebTransportStopSendingCapsule::ToString() const {
  return absl::StrCat("WT_STOP_SENDING(stream_id=", stream_id,
                      ", error_code=", error_code, ")");
}

std::string WebTransportMaxStreamDataCapsule::ToString() const {
  return absl::StrCat("WT_MAX_STREAM_DATA (stream_id=", stream_id,
                      ", max_stream_data=", max_stream_data, ")");
}

std::string WebTransportMaxStreamsCapsule::ToString() const {
  return absl::StrCat(CapsuleTypeToString(capsule_type()),
                      " (max_streams=", max_stream_count, ")");
}

std::string Capsule::ToString() const {
  return absl::visit([](const auto& capsule) { return capsule.ToString(); },
                     capsule_);
}

std::ostream& operator<<(std::ostream& os, const Capsule& capsule) {
  os << capsule.ToString();
  return os;
}

CapsuleParser::CapsuleParser(Visitor* visitor) : visitor_(visitor) {
  QUICHE_DCHECK_NE(visitor_, nullptr);
}

// Serialization logic for quiche::PrefixWithId.
class WirePrefixWithId {
 public:
  using DataType = PrefixWithId;

  WirePrefixWithId(const PrefixWithId& prefix) : prefix_(prefix) {}

  size_t GetLengthOnWire() {
    return ComputeLengthOnWire(
        WireVarInt62(prefix_.request_id),
        WireUint8(prefix_.ip_prefix.address().IsIPv4() ? 4 : 6),
        WireBytes(prefix_.ip_prefix.address().ToPackedString()),
        WireUint8(prefix_.ip_prefix.prefix_length()));
  }

  absl::Status SerializeIntoWriter(QuicheDataWriter& writer) {
    return AppendToStatus(
        quiche::SerializeIntoWriter(
            writer, WireVarInt62(prefix_.request_id),
            WireUint8(prefix_.ip_prefix.address().IsIPv4() ? 4 : 6),
            WireBytes(prefix_.ip_prefix.address().ToPackedString()),
            WireUint8(prefix_.ip_prefix.prefix_length())),
        " while serializing a PrefixWithId");
  }

 private:
  const PrefixWithId& prefix_;
};

// Serialization logic for quiche::IpAddressRange.
class WireIpAddressRange {
 public:
  using DataType = IpAddressRange;

  explicit WireIpAddressRange(const IpAddressRange& range) : range_(range) {}

  size_t GetLengthOnWire() {
    return ComputeLengthOnWire(
        WireUint8(range_.start_ip_address.IsIPv4() ? 4 : 6),
        WireBytes(range_.start_ip_address.ToPackedString()),
        WireBytes(range_.end_ip_address.ToPackedString()),
        WireUint8(range_.ip_protocol));
  }

  absl::Status SerializeIntoWriter(QuicheDataWriter& writer) {
    return AppendToStatus(
        ::quiche::SerializeIntoWriter(
            writer, WireUint8(range_.start_ip_address.IsIPv4() ? 4 : 6),
            WireBytes(range_.start_ip_address.ToPackedString()),
            WireBytes(range_.end_ip_address.ToPackedString()),
            WireUint8(range_.ip_protocol)),
        " while serializing an IpAddressRange");
  }

 private:
  const IpAddressRange& range_;
};

template <typename... T>
absl::StatusOr<quiche::QuicheBuffer> SerializeCapsuleFields(
    CapsuleType type, QuicheBufferAllocator* allocator, T... fields) {
  size_t capsule_payload_size = ComputeLengthOnWire(fields...);
  return SerializeIntoBuffer(allocator, WireVarInt62(type),
                             WireVarInt62(capsule_payload_size), fields...);
}

absl::StatusOr<quiche::QuicheBuffer> SerializeCapsuleWithStatus(
    const Capsule& capsule, quiche::QuicheBufferAllocator* allocator) {
  switch (capsule.capsule_type()) {
    case CapsuleType::DATAGRAM:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireBytes(capsule.datagram_capsule().http_datagram_payload));
    case CapsuleType::LEGACY_DATAGRAM:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireBytes(capsule.legacy_datagram_capsule().http_datagram_payload));
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireBytes(capsule.legacy_datagram_without_context_capsule()
                        .http_datagram_payload));
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireUint32(capsule.close_web_transport_session_capsule().error_code),
          WireBytes(
              capsule.close_web_transport_session_capsule().error_message));
    case CapsuleType::DRAIN_WEBTRANSPORT_SESSION:
      return SerializeCapsuleFields(capsule.capsule_type(), allocator);
    case CapsuleType::ADDRESS_REQUEST:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireSpan<WirePrefixWithId>(absl::MakeConstSpan(
              capsule.address_request_capsule().requested_addresses)));
    case CapsuleType::ADDRESS_ASSIGN:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireSpan<WirePrefixWithId>(absl::MakeConstSpan(
              capsule.address_assign_capsule().assigned_addresses)));
    case CapsuleType::ROUTE_ADVERTISEMENT:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireSpan<WireIpAddressRange>(absl::MakeConstSpan(
              capsule.route_advertisement_capsule().ip_address_ranges)));
    case CapsuleType::WT_STREAM:
    case CapsuleType::WT_STREAM_WITH_FIN:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireVarInt62(capsule.web_transport_stream_data().stream_id),
          WireBytes(capsule.web_transport_stream_data().data));
    case CapsuleType::WT_RESET_STREAM:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireVarInt62(capsule.web_transport_reset_stream().stream_id),
          WireVarInt62(capsule.web_transport_reset_stream().error_code));
    case CapsuleType::WT_STOP_SENDING:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireVarInt62(capsule.web_transport_stop_sending().stream_id),
          WireVarInt62(capsule.web_transport_stop_sending().error_code));
    case CapsuleType::WT_MAX_STREAM_DATA:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireVarInt62(capsule.web_transport_max_stream_data().stream_id),
          WireVarInt62(
              capsule.web_transport_max_stream_data().max_stream_data));
    case CapsuleType::WT_MAX_STREAMS_BIDI:
    case CapsuleType::WT_MAX_STREAMS_UNIDI:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireVarInt62(capsule.web_transport_max_streams().max_stream_count));
    default:
      return SerializeCapsuleFields(
          capsule.capsule_type(), allocator,
          WireBytes(capsule.unknown_capsule().payload));
  }
}

QuicheBuffer SerializeDatagramCapsuleHeader(uint64_t datagram_size,
                                            QuicheBufferAllocator* allocator) {
  absl::StatusOr<QuicheBuffer> buffer =
      SerializeIntoBuffer(allocator, WireVarInt62(CapsuleType::DATAGRAM),
                          WireVarInt62(datagram_size));
  if (!buffer.ok()) {
    return QuicheBuffer();
  }
  return *std::move(buffer);
}

QUICHE_EXPORT QuicheBuffer SerializeWebTransportStreamCapsuleHeader(
    webtransport::StreamId stream_id, bool fin, uint64_t write_size,
    QuicheBufferAllocator* allocator) {
  absl::StatusOr<QuicheBuffer> buffer = SerializeIntoBuffer(
      allocator,
      WireVarInt62(fin ? CapsuleType::WT_STREAM_WITH_FIN
                       : CapsuleType::WT_STREAM),
      WireVarInt62(write_size + QuicheDataWriter::GetVarInt62Len(stream_id)),
      WireVarInt62(stream_id));
  if (!buffer.ok()) {
    return QuicheBuffer();
  }
  return *std::move(buffer);
}

QuicheBuffer SerializeCapsule(const Capsule& capsule,
                              quiche::QuicheBufferAllocator* allocator) {
  absl::StatusOr<QuicheBuffer> serialized =
      SerializeCapsuleWithStatus(capsule, allocator);
  if (!serialized.ok()) {
    QUICHE_BUG(capsule_serialization_failed)
        << "Failed to serialize the following capsule:\n"
        << capsule << "Serialization error: " << serialized.status();
    return QuicheBuffer();
  }
  return *std::move(serialized);
}

bool CapsuleParser::IngestCapsuleFragment(absl::string_view capsule_fragment) {
  if (parsing_error_occurred_) {
    return false;
  }
  absl::StrAppend(&buffered_data_, capsule_fragment);
  while (true) {
    const absl::StatusOr<size_t> buffered_data_read = AttemptParseCapsule();
    if (!buffered_data_read.ok()) {
      ReportParseFailure(buffered_data_read.status().message());
      buffered_data_.clear();
      return false;
    }
    if (*buffered_data_read == 0) {
      break;
    }
    buffered_data_.erase(0, *buffered_data_read);
  }
  static constexpr size_t kMaxCapsuleBufferSize = 1024 * 1024;
  if (buffered_data_.size() > kMaxCapsuleBufferSize) {
    buffered_data_.clear();
    ReportParseFailure("Refusing to buffer too much capsule data");
    return false;
  }
  return true;
}

namespace {
absl::Status ReadWebTransportStreamId(QuicheDataReader& reader,
                                      webtransport::StreamId& id) {
  uint64_t raw_id;
  if (!reader.ReadVarInt62(&raw_id)) {
    return absl::InvalidArgumentError("Failed to read WebTransport Stream ID");
  }
  if (raw_id > std::numeric_limits<uint32_t>::max()) {
    return absl::InvalidArgumentError("Stream ID does not fit into a uint32_t");
  }
  id = static_cast<webtransport::StreamId>(raw_id);
  return absl::OkStatus();
}

absl::StatusOr<Capsule> ParseCapsulePayload(QuicheDataReader& reader,
                                            CapsuleType type) {
  switch (type) {
    case CapsuleType::DATAGRAM:
      return Capsule::Datagram(reader.ReadRemainingPayload());
    case CapsuleType::LEGACY_DATAGRAM:
      return Capsule::LegacyDatagram(reader.ReadRemainingPayload());
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT:
      return Capsule::LegacyDatagramWithoutContext(
          reader.ReadRemainingPayload());
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION: {
      CloseWebTransportSessionCapsule capsule;
      if (!reader.ReadUInt32(&capsule.error_code)) {
        return absl::InvalidArgumentError(
            "Unable to parse capsule CLOSE_WEBTRANSPORT_SESSION error code");
      }
      capsule.error_message = reader.ReadRemainingPayload();
      return Capsule(std::move(capsule));
    }
    case CapsuleType::DRAIN_WEBTRANSPORT_SESSION:
      return Capsule(DrainWebTransportSessionCapsule());
    case CapsuleType::ADDRESS_REQUEST: {
      AddressRequestCapsule capsule;
      while (!reader.IsDoneReading()) {
        PrefixWithId requested_address;
        if (!reader.ReadVarInt62(&requested_address.request_id)) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ADDRESS_REQUEST request ID");
        }
        uint8_t address_family;
        if (!reader.ReadUInt8(&address_family)) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ADDRESS_REQUEST family");
        }
        if (address_family != 4 && address_family != 6) {
          return absl::InvalidArgumentError("Bad ADDRESS_REQUEST family");
        }
        absl::string_view ip_address_bytes;
        if (!reader.ReadStringPiece(&ip_address_bytes,
                                    address_family == 4
                                        ? QuicheIpAddress::kIPv4AddressSize
                                        : QuicheIpAddress::kIPv6AddressSize)) {
          return absl::InvalidArgumentError(
              "Unable to read capsule ADDRESS_REQUEST address");
        }
        quiche::QuicheIpAddress ip_address;
        if (!ip_address.FromPackedString(ip_address_bytes.data(),
                                         ip_address_bytes.size())) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ADDRESS_REQUEST address");
        }
        uint8_t ip_prefix_length;
        if (!reader.ReadUInt8(&ip_prefix_length)) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ADDRESS_REQUEST IP prefix length");
        }
        if (ip_prefix_length > QuicheIpPrefix(ip_address).prefix_length()) {
          return absl::InvalidArgumentError("Invalid IP prefix length");
        }
        requested_address.ip_prefix =
            QuicheIpPrefix(ip_address, ip_prefix_length);
        capsule.requested_addresses.push_back(requested_address);
      }
      return Capsule(std::move(capsule));
    }
    case CapsuleType::ADDRESS_ASSIGN: {
      AddressAssignCapsule capsule;
      while (!reader.IsDoneReading()) {
        PrefixWithId assigned_address;
        if (!reader.ReadVarInt62(&assigned_address.request_id)) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ADDRESS_ASSIGN request ID");
        }
        uint8_t address_family;
        if (!reader.ReadUInt8(&address_family)) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ADDRESS_ASSIGN family");
        }
        if (address_family != 4 && address_family != 6) {
          return absl::InvalidArgumentError("Bad ADDRESS_ASSIGN family");
        }
        absl::string_view ip_address_bytes;
        if (!reader.ReadStringPiece(&ip_address_bytes,
                                    address_family == 4
                                        ? QuicheIpAddress::kIPv4AddressSize
                                        : QuicheIpAddress::kIPv6AddressSize)) {
          return absl::InvalidArgumentError(
              "Unable to read capsule ADDRESS_ASSIGN address");
        }
        quiche::QuicheIpAddress ip_address;
        if (!ip_address.FromPackedString(ip_address_bytes.data(),
                                         ip_address_bytes.size())) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ADDRESS_ASSIGN address");
        }
        uint8_t ip_prefix_length;
        if (!reader.ReadUInt8(&ip_prefix_length)) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ADDRESS_ASSIGN IP prefix length");
        }
        if (ip_prefix_length > QuicheIpPrefix(ip_address).prefix_length()) {
          return absl::InvalidArgumentError("Invalid IP prefix length");
        }
        assigned_address.ip_prefix =
            QuicheIpPrefix(ip_address, ip_prefix_length);
        capsule.assigned_addresses.push_back(assigned_address);
      }
      return Capsule(std::move(capsule));
    }
    case CapsuleType::ROUTE_ADVERTISEMENT: {
      RouteAdvertisementCapsule capsule;
      while (!reader.IsDoneReading()) {
        uint8_t address_family;
        if (!reader.ReadUInt8(&address_family)) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ROUTE_ADVERTISEMENT family");
        }
        if (address_family != 4 && address_family != 6) {
          return absl::InvalidArgumentError("Bad ROUTE_ADVERTISEMENT family");
        }
        IpAddressRange ip_address_range;
        absl::string_view start_ip_address_bytes;
        if (!reader.ReadStringPiece(&start_ip_address_bytes,
                                    address_family == 4
                                        ? QuicheIpAddress::kIPv4AddressSize
                                        : QuicheIpAddress::kIPv6AddressSize)) {
          return absl::InvalidArgumentError(
              "Unable to read capsule ROUTE_ADVERTISEMENT start address");
        }
        if (!ip_address_range.start_ip_address.FromPackedString(
                start_ip_address_bytes.data(), start_ip_address_bytes.size())) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ROUTE_ADVERTISEMENT start address");
        }
        absl::string_view end_ip_address_bytes;
        if (!reader.ReadStringPiece(&end_ip_address_bytes,
                                    address_family == 4
                                        ? QuicheIpAddress::kIPv4AddressSize
                                        : QuicheIpAddress::kIPv6AddressSize)) {
          return absl::InvalidArgumentError(
              "Unable to read capsule ROUTE_ADVERTISEMENT end address");
        }
        if (!ip_address_range.end_ip_address.FromPackedString(
                end_ip_address_bytes.data(), end_ip_address_bytes.size())) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ROUTE_ADVERTISEMENT end address");
        }
        if (!reader.ReadUInt8(&ip_address_range.ip_protocol)) {
          return absl::InvalidArgumentError(
              "Unable to parse capsule ROUTE_ADVERTISEMENT IP protocol");
        }
        capsule.ip_address_ranges.push_back(ip_address_range);
      }
      return Capsule(std::move(capsule));
    }
    case CapsuleType::WT_STREAM:
    case CapsuleType::WT_STREAM_WITH_FIN: {
      WebTransportStreamDataCapsule capsule;
      capsule.fin = (type == CapsuleType::WT_STREAM_WITH_FIN);
      QUICHE_RETURN_IF_ERROR(
          ReadWebTransportStreamId(reader, capsule.stream_id));
      capsule.data = reader.ReadRemainingPayload();
      return Capsule(std::move(capsule));
    }
    case CapsuleType::WT_RESET_STREAM: {
      WebTransportResetStreamCapsule capsule;
      QUICHE_RETURN_IF_ERROR(
          ReadWebTransportStreamId(reader, capsule.stream_id));
      if (!reader.ReadVarInt62(&capsule.error_code)) {
        return absl::InvalidArgumentError(
            "Failed to parse the RESET_STREAM error code");
      }
      return Capsule(std::move(capsule));
    }
    case CapsuleType::WT_STOP_SENDING: {
      WebTransportStopSendingCapsule capsule;
      QUICHE_RETURN_IF_ERROR(
          ReadWebTransportStreamId(reader, capsule.stream_id));
      if (!reader.ReadVarInt62(&capsule.error_code)) {
        return absl::InvalidArgumentError(
            "Failed to parse the STOP_SENDING error code");
      }
      return Capsule(std::move(capsule));
    }
    case CapsuleType::WT_MAX_STREAM_DATA: {
      WebTransportMaxStreamDataCapsule capsule;
      QUICHE_RETURN_IF_ERROR(
          ReadWebTransportStreamId(reader, capsule.stream_id));
      if (!reader.ReadVarInt62(&capsule.max_stream_data)) {
        return absl::InvalidArgumentError(
            "Failed to parse the max stream data field");
      }
      return Capsule(std::move(capsule));
    }
    case CapsuleType::WT_MAX_STREAMS_UNIDI:
    case CapsuleType::WT_MAX_STREAMS_BIDI: {
      WebTransportMaxStreamsCapsule capsule;
      capsule.stream_type = type == CapsuleType::WT_MAX_STREAMS_UNIDI
                                ? webtransport::StreamType::kUnidirectional
                                : webtransport::StreamType::kBidirectional;
      if (!reader.ReadVarInt62(&capsule.max_stream_count)) {
        return absl::InvalidArgumentError(
            "Failed to parse the max streams field");
      }
      return Capsule(std::move(capsule));
    }
    default:
      return Capsule(UnknownCapsule{static_cast<uint64_t>(type),
                                    reader.ReadRemainingPayload()});
  }
}
}  // namespace

absl::StatusOr<size_t> CapsuleParser::AttemptParseCapsule() {
  QUICHE_DCHECK(!parsing_error_occurred_);
  if (buffered_data_.empty()) {
    return 0;
  }
  QuicheDataReader capsule_fragment_reader(buffered_data_);
  uint64_t capsule_type64;
  if (!capsule_fragment_reader.ReadVarInt62(&capsule_type64)) {
    QUICHE_DVLOG(2) << "Partial read: not enough data to read capsule type";
    return 0;
  }
  absl::string_view capsule_data;
  if (!capsule_fragment_reader.ReadStringPieceVarInt62(&capsule_data)) {
    QUICHE_DVLOG(2)
        << "Partial read: not enough data to read capsule length or "
           "full capsule data";
    return 0;
  }
  QuicheDataReader capsule_data_reader(capsule_data);
  absl::StatusOr<Capsule> capsule = ParseCapsulePayload(
      capsule_data_reader, static_cast<CapsuleType>(capsule_type64));
  QUICHE_RETURN_IF_ERROR(capsule.status());
  if (!visitor_->OnCapsule(*capsule)) {
    return absl::AbortedError("Visitor failed to process capsule");
  }
  return capsule_fragment_reader.PreviouslyReadPayload().length();
}

void CapsuleParser::ReportParseFailure(absl::string_view error_message) {
  if (parsing_error_occurred_) {
    QUICHE_BUG(multiple parse errors) << "Experienced multiple parse failures";
    return;
  }
  parsing_error_occurred_ = true;
  visitor_->OnCapsuleParseFailure(error_message);
}

void CapsuleParser::ErrorIfThereIsRemainingBufferedData() {
  if (parsing_error_occurred_) {
    return;
  }
  if (!buffered_data_.empty()) {
    ReportParseFailure("Incomplete capsule left at the end of the stream");
  }
}

bool PrefixWithId::operator==(const PrefixWithId& other) const {
  return request_id == other.request_id && ip_prefix == other.ip_prefix;
}

bool IpAddressRange::operator==(const IpAddressRange& other) const {
  return start_ip_address == other.start_ip_address &&
         end_ip_address == other.end_ip_address &&
         ip_protocol == other.ip_protocol;
}

bool AddressAssignCapsule::operator==(const AddressAssignCapsule& other) const {
  return assigned_addresses == other.assigned_addresses;
}

bool AddressRequestCapsule::operator==(
    const AddressRequestCapsule& other) const {
  return requested_addresses == other.requested_addresses;
}

bool RouteAdvertisementCapsule::operator==(
    const RouteAdvertisementCapsule& other) const {
  return ip_address_ranges == other.ip_address_ranges;
}

bool WebTransportStreamDataCapsule::operator==(
    const WebTransportStreamDataCapsule& other) const {
  return stream_id == other.stream_id && data == other.data && fin == other.fin;
}

bool WebTransportResetStreamCapsule::operator==(
    const WebTransportResetStreamCapsule& other) const {
  return stream_id == other.stream_id && error_code == other.error_code;
}

bool WebTransportStopSendingCapsule::operator==(
    const WebTransportStopSendingCapsule& other) const {
  return stream_id == other.stream_id && error_code == other.error_code;
}

bool WebTransportMaxStreamDataCapsule::operator==(
    const WebTransportMaxStreamDataCapsule& other) const {
  return stream_id == other.stream_id &&
         max_stream_data == other.max_stream_data;
}

bool WebTransportMaxStreamsCapsule::operator==(
    const WebTransportMaxStreamsCapsule& other) const {
  return stream_type == other.stream_type &&
         max_stream_count == other.max_stream_count;
}

}  // namespace quiche
