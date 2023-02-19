// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/capsule.h"

#include <type_traits>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_ip_address.h"

namespace quic {

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
    case CapsuleType::ADDRESS_REQUEST:
      return "ADDRESS_REQUEST";
    case CapsuleType::ADDRESS_ASSIGN:
      return "ADDRESS_ASSIGN";
    case CapsuleType::ROUTE_ADVERTISEMENT:
      return "ROUTE_ADVERTISEMENT";
  }
  return absl::StrCat("Unknown(", static_cast<uint64_t>(capsule_type), ")");
}

std::ostream& operator<<(std::ostream& os, const CapsuleType& capsule_type) {
  os << CapsuleTypeToString(capsule_type);
  return os;
}

Capsule::Capsule(CapsuleType capsule_type) : capsule_type_(capsule_type) {
  switch (capsule_type) {
    case CapsuleType::DATAGRAM:
      static_assert(std::is_standard_layout<DatagramCapsule>::value &&
                        std::is_trivially_destructible<DatagramCapsule>::value,
                    "All inline capsule structs must have these properties");
      datagram_capsule_ = DatagramCapsule();
      break;
    case CapsuleType::LEGACY_DATAGRAM:
      static_assert(
          std::is_standard_layout<LegacyDatagramCapsule>::value &&
              std::is_trivially_destructible<LegacyDatagramCapsule>::value,
          "All inline capsule structs must have these properties");
      legacy_datagram_capsule_ = LegacyDatagramCapsule();
      break;
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT:
      static_assert(
          std::is_standard_layout<LegacyDatagramWithoutContextCapsule>::value &&
              std::is_trivially_destructible<
                  LegacyDatagramWithoutContextCapsule>::value,
          "All inline capsule structs must have these properties");
      legacy_datagram_without_context_capsule_ =
          LegacyDatagramWithoutContextCapsule();
      break;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      static_assert(
          std::is_standard_layout<CloseWebTransportSessionCapsule>::value &&
              std::is_trivially_destructible<
                  CloseWebTransportSessionCapsule>::value,
          "All inline capsule structs must have these properties");
      close_web_transport_session_capsule_ = CloseWebTransportSessionCapsule();
      break;
    case CapsuleType::ADDRESS_REQUEST:
      address_request_capsule_ = new AddressRequestCapsule();
      break;
    case CapsuleType::ADDRESS_ASSIGN:
      address_assign_capsule_ = new AddressAssignCapsule();
      break;
    case CapsuleType::ROUTE_ADVERTISEMENT:
      route_advertisement_capsule_ = new RouteAdvertisementCapsule();
      break;
    default:
      unknown_capsule_data_ = absl::string_view();
      break;
  }
}

void Capsule::Free() {
  switch (capsule_type_) {
    // Inlined capsule types.
    case CapsuleType::DATAGRAM:
    case CapsuleType::LEGACY_DATAGRAM:
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT:
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      // Do nothing, these are guaranteed to be trivially destructible.
      break;
    // Out-of-line capsule types.
    case CapsuleType::ADDRESS_REQUEST:
      delete address_request_capsule_;
      break;
    case CapsuleType::ADDRESS_ASSIGN:
      delete address_assign_capsule_;
      break;
    case CapsuleType::ROUTE_ADVERTISEMENT:
      delete route_advertisement_capsule_;
      break;
  }
  capsule_type_ = static_cast<CapsuleType>(0x17);  // Reserved unknown value.
  unknown_capsule_data_ = absl::string_view();
}
Capsule::~Capsule() { Free(); }

// static
Capsule Capsule::Datagram(absl::string_view http_datagram_payload) {
  Capsule capsule(CapsuleType::DATAGRAM);
  capsule.datagram_capsule().http_datagram_payload = http_datagram_payload;
  return capsule;
}

// static
Capsule Capsule::LegacyDatagram(absl::string_view http_datagram_payload) {
  Capsule capsule(CapsuleType::LEGACY_DATAGRAM);
  capsule.legacy_datagram_capsule().http_datagram_payload =
      http_datagram_payload;
  return capsule;
}

// static
Capsule Capsule::LegacyDatagramWithoutContext(
    absl::string_view http_datagram_payload) {
  Capsule capsule(CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT);
  capsule.legacy_datagram_without_context_capsule().http_datagram_payload =
      http_datagram_payload;
  return capsule;
}

// static
Capsule Capsule::CloseWebTransportSession(WebTransportSessionError error_code,
                                          absl::string_view error_message) {
  Capsule capsule(CapsuleType::CLOSE_WEBTRANSPORT_SESSION);
  capsule.close_web_transport_session_capsule().error_code = error_code;
  capsule.close_web_transport_session_capsule().error_message = error_message;
  return capsule;
}

// static
Capsule Capsule::AddressRequest() {
  return Capsule(CapsuleType::ADDRESS_REQUEST);
}

// static
Capsule Capsule::AddressAssign() {
  return Capsule(CapsuleType::ADDRESS_ASSIGN);
}

// static
Capsule Capsule::RouteAdvertisement() {
  return Capsule(CapsuleType::ROUTE_ADVERTISEMENT);
}

// static
Capsule Capsule::Unknown(uint64_t capsule_type,
                         absl::string_view unknown_capsule_data) {
  Capsule capsule(static_cast<CapsuleType>(capsule_type));
  capsule.unknown_capsule_data() = unknown_capsule_data;
  return capsule;
}

Capsule& Capsule::operator=(const Capsule& other) {
  Free();
  capsule_type_ = other.capsule_type_;
  switch (capsule_type_) {
    case CapsuleType::DATAGRAM:
      datagram_capsule_ = other.datagram_capsule_;
      break;
    case CapsuleType::LEGACY_DATAGRAM:
      legacy_datagram_capsule_ = other.legacy_datagram_capsule_;
      break;
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT:
      legacy_datagram_without_context_capsule_ =
          other.legacy_datagram_without_context_capsule_;
      break;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      close_web_transport_session_capsule_ =
          other.close_web_transport_session_capsule_;
      break;
    case CapsuleType::ADDRESS_ASSIGN:
      address_assign_capsule_ = new AddressAssignCapsule();
      *address_assign_capsule_ = *other.address_assign_capsule_;
      break;
    case CapsuleType::ADDRESS_REQUEST:
      address_request_capsule_ = new AddressRequestCapsule();
      *address_request_capsule_ = *other.address_request_capsule_;
      break;
    case CapsuleType::ROUTE_ADVERTISEMENT:
      route_advertisement_capsule_ = new RouteAdvertisementCapsule();
      *route_advertisement_capsule_ = *other.route_advertisement_capsule_;
      break;
    default:
      unknown_capsule_data_ = other.unknown_capsule_data_;
      break;
  }
  return *this;
}

Capsule::Capsule(const Capsule& other) : Capsule(other.capsule_type_) {
  *this = other;
}

bool Capsule::operator==(const Capsule& other) const {
  if (capsule_type_ != other.capsule_type_) {
    return false;
  }
  switch (capsule_type_) {
    case CapsuleType::DATAGRAM:
      return datagram_capsule_.http_datagram_payload ==
             other.datagram_capsule_.http_datagram_payload;
    case CapsuleType::LEGACY_DATAGRAM:
      return legacy_datagram_capsule_.http_datagram_payload ==
             other.legacy_datagram_capsule_.http_datagram_payload;
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT:
      return legacy_datagram_without_context_capsule_.http_datagram_payload ==
             other.legacy_datagram_without_context_capsule_
                 .http_datagram_payload;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      return close_web_transport_session_capsule_.error_code ==
                 other.close_web_transport_session_capsule_.error_code &&
             close_web_transport_session_capsule_.error_message ==
                 other.close_web_transport_session_capsule_.error_message;
    case CapsuleType::ADDRESS_REQUEST:
      return address_request_capsule_->requested_addresses ==
             other.address_request_capsule_->requested_addresses;
    case CapsuleType::ADDRESS_ASSIGN:
      return address_assign_capsule_->assigned_addresses ==
             other.address_assign_capsule_->assigned_addresses;
    case CapsuleType::ROUTE_ADVERTISEMENT:
      return route_advertisement_capsule_->ip_address_ranges ==
             other.route_advertisement_capsule_->ip_address_ranges;
    default:
      return unknown_capsule_data_ == other.unknown_capsule_data_;
  }
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

std::string Capsule::ToString() const {
  switch (capsule_type_) {
    case CapsuleType::DATAGRAM:
      return datagram_capsule_.ToString();
    case CapsuleType::LEGACY_DATAGRAM:
      return legacy_datagram_capsule_.ToString();
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT:
      return legacy_datagram_without_context_capsule_.ToString();
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      return close_web_transport_session_capsule_.ToString();
    case CapsuleType::ADDRESS_REQUEST:
      return address_request_capsule_->ToString();
    case CapsuleType::ADDRESS_ASSIGN:
      return address_assign_capsule_->ToString();
    case CapsuleType::ROUTE_ADVERTISEMENT:
      return route_advertisement_capsule_->ToString();
    default:
      return absl::StrCat(CapsuleTypeToString(capsule_type_), "[",
                          absl::BytesToHexString(unknown_capsule_data_), "]");
  }
}

std::ostream& operator<<(std::ostream& os, const Capsule& capsule) {
  os << capsule.ToString();
  return os;
}

CapsuleParser::CapsuleParser(Visitor* visitor) : visitor_(visitor) {
  QUICHE_DCHECK_NE(visitor_, nullptr);
}

quiche::QuicheBuffer SerializeCapsule(
    const Capsule& capsule, quiche::QuicheBufferAllocator* allocator) {
  QuicByteCount capsule_type_length = QuicDataWriter::GetVarInt62Len(
      static_cast<uint64_t>(capsule.capsule_type()));
  QuicByteCount capsule_data_length;
  switch (capsule.capsule_type()) {
    case CapsuleType::DATAGRAM:
      capsule_data_length =
          capsule.datagram_capsule().http_datagram_payload.length();
      break;
    case CapsuleType::LEGACY_DATAGRAM:
      capsule_data_length =
          capsule.legacy_datagram_capsule().http_datagram_payload.length();
      break;
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT:
      capsule_data_length = capsule.legacy_datagram_without_context_capsule()
                                .http_datagram_payload.length();
      break;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      capsule_data_length =
          sizeof(WebTransportSessionError) +
          capsule.close_web_transport_session_capsule().error_message.size();
      break;
    case CapsuleType::ADDRESS_REQUEST:
      capsule_data_length = 0;
      for (auto requested_address :
           capsule.address_request_capsule().requested_addresses) {
        capsule_data_length +=
            QuicDataWriter::GetVarInt62Len(requested_address.request_id) + 1 +
            (requested_address.ip_prefix.address().IsIPv4()
                 ? QuicIpAddress::kIPv4AddressSize
                 : QuicIpAddress::kIPv6AddressSize) +
            1;
      }
      break;
    case CapsuleType::ADDRESS_ASSIGN:
      capsule_data_length = 0;
      for (auto assigned_address :
           capsule.address_assign_capsule().assigned_addresses) {
        capsule_data_length +=
            QuicDataWriter::GetVarInt62Len(assigned_address.request_id) + 1 +
            (assigned_address.ip_prefix.address().IsIPv4()
                 ? QuicIpAddress::kIPv4AddressSize
                 : QuicIpAddress::kIPv6AddressSize) +
            1;
      }
      break;
    case CapsuleType::ROUTE_ADVERTISEMENT:
      capsule_data_length = 0;
      for (auto ip_address_range :
           capsule.route_advertisement_capsule().ip_address_ranges) {
        capsule_data_length += 1 +
                               (ip_address_range.start_ip_address.IsIPv4()
                                    ? QuicIpAddress::kIPv4AddressSize
                                    : QuicIpAddress::kIPv6AddressSize) *
                                   2 +
                               1;
      }
      break;
    default:
      capsule_data_length = capsule.unknown_capsule_data().length();
      break;
  }
  QuicByteCount capsule_length_length =
      QuicDataWriter::GetVarInt62Len(capsule_data_length);
  QuicByteCount total_capsule_length =
      capsule_type_length + capsule_length_length + capsule_data_length;
  quiche::QuicheBuffer buffer(allocator, total_capsule_length);
  QuicDataWriter writer(buffer.size(), buffer.data());
  if (!writer.WriteVarInt62(static_cast<uint64_t>(capsule.capsule_type()))) {
    QUIC_BUG(capsule type write fail) << "Failed to write CAPSULE type";
    return {};
  }
  if (!writer.WriteVarInt62(capsule_data_length)) {
    QUIC_BUG(capsule length write fail) << "Failed to write CAPSULE length";
    return {};
  }
  switch (capsule.capsule_type()) {
    case CapsuleType::DATAGRAM:
      if (!writer.WriteStringPiece(
              capsule.datagram_capsule().http_datagram_payload)) {
        QUIC_BUG(datagram capsule payload write fail)
            << "Failed to write DATAGRAM CAPSULE payload";
        return {};
      }
      break;
    case CapsuleType::LEGACY_DATAGRAM:
      if (!writer.WriteStringPiece(
              capsule.legacy_datagram_capsule().http_datagram_payload)) {
        QUIC_BUG(datagram legacy capsule payload write fail)
            << "Failed to write LEGACY_DATAGRAM CAPSULE payload";
        return {};
      }
      break;
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT:
      if (!writer.WriteStringPiece(
              capsule.legacy_datagram_without_context_capsule()
                  .http_datagram_payload)) {
        QUIC_BUG(datagram legacy without context capsule payload write fail)
            << "Failed to write LEGACY_DATAGRAM_WITHOUT_CONTEXT CAPSULE "
               "payload";
        return {};
      }
      break;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      if (!writer.WriteUInt32(
              capsule.close_web_transport_session_capsule().error_code)) {
        QUIC_BUG(close webtransport session capsule error code write fail)
            << "Failed to write CLOSE_WEBTRANSPORT_SESSION error code";
        return {};
      }
      if (!writer.WriteStringPiece(
              capsule.close_web_transport_session_capsule().error_message)) {
        QUIC_BUG(close webtransport session capsule error message write fail)
            << "Failed to write CLOSE_WEBTRANSPORT_SESSION error message";
        return {};
      }
      break;
    case CapsuleType::ADDRESS_REQUEST:
      for (auto requested_address :
           capsule.address_request_capsule().requested_addresses) {
        if (!writer.WriteVarInt62(requested_address.request_id)) {
          QUIC_BUG(address request capsule id write fail)
              << "Failed to write ADDRESS_REQUEST ID";
          return {};
        }
        if (!writer.WriteUInt8(
                requested_address.ip_prefix.address().IsIPv4() ? 4 : 6)) {
          QUIC_BUG(address request capsule family write fail)
              << "Failed to write ADDRESS_REQUEST family";
          return {};
        }
        if (!writer.WriteStringPiece(
                requested_address.ip_prefix.address().ToPackedString())) {
          QUIC_BUG(address request capsule address write fail)
              << "Failed to write ADDRESS_REQUEST address";
          return {};
        }
        if (!writer.WriteUInt8(requested_address.ip_prefix.prefix_length())) {
          QUIC_BUG(address request capsule prefix length write fail)
              << "Failed to write ADDRESS_REQUEST prefix length";
          return {};
        }
      }
      break;
    case CapsuleType::ADDRESS_ASSIGN:
      for (auto assigned_address :
           capsule.address_assign_capsule().assigned_addresses) {
        if (!writer.WriteVarInt62(assigned_address.request_id)) {
          QUIC_BUG(address request capsule id write fail)
              << "Failed to write ADDRESS_ASSIGN ID";
          return {};
        }
        if (!writer.WriteUInt8(
                assigned_address.ip_prefix.address().IsIPv4() ? 4 : 6)) {
          QUIC_BUG(address request capsule family write fail)
              << "Failed to write ADDRESS_ASSIGN family";
          return {};
        }
        if (!writer.WriteStringPiece(
                assigned_address.ip_prefix.address().ToPackedString())) {
          QUIC_BUG(address request capsule address write fail)
              << "Failed to write ADDRESS_ASSIGN address";
          return {};
        }
        if (!writer.WriteUInt8(assigned_address.ip_prefix.prefix_length())) {
          QUIC_BUG(address request capsule prefix length write fail)
              << "Failed to write ADDRESS_ASSIGN prefix length";
          return {};
        }
      }
      break;
    case CapsuleType::ROUTE_ADVERTISEMENT:
      for (auto ip_address_range :
           capsule.route_advertisement_capsule().ip_address_ranges) {
        if (!writer.WriteUInt8(
                ip_address_range.start_ip_address.IsIPv4() ? 4 : 6)) {
          QUIC_BUG(route advertisement capsule family write fail)
              << "Failed to write ROUTE_ADVERTISEMENT family";
          return {};
        }
        if (!writer.WriteStringPiece(
                ip_address_range.start_ip_address.ToPackedString())) {
          QUIC_BUG(route advertisement capsule start address write fail)
              << "Failed to write ROUTE_ADVERTISEMENT start address";
          return {};
        }
        if (!writer.WriteStringPiece(
                ip_address_range.end_ip_address.ToPackedString())) {
          QUIC_BUG(route advertisement capsule end address write fail)
              << "Failed to write ROUTE_ADVERTISEMENT end address";
          return {};
        }
        if (!writer.WriteUInt8(ip_address_range.ip_protocol)) {
          QUIC_BUG(route advertisement capsule IP protocol write fail)
              << "Failed to write ROUTE_ADVERTISEMENT IP protocol";
          return {};
        }
      }
      break;
    default:
      if (!writer.WriteStringPiece(capsule.unknown_capsule_data())) {
        QUIC_BUG(capsule data write fail) << "Failed to write CAPSULE data";
        return {};
      }
      break;
  }
  if (writer.remaining() != 0) {
    QUIC_BUG(capsule write length mismatch)
        << "CAPSULE serialization wrote " << writer.length() << " instead of "
        << writer.capacity();
    return {};
  }
  return buffer;
}

bool CapsuleParser::IngestCapsuleFragment(absl::string_view capsule_fragment) {
  if (parsing_error_occurred_) {
    return false;
  }
  absl::StrAppend(&buffered_data_, capsule_fragment);
  while (true) {
    const size_t buffered_data_read = AttemptParseCapsule();
    if (parsing_error_occurred_) {
      QUICHE_DCHECK_EQ(buffered_data_read, 0u);
      buffered_data_.clear();
      return false;
    }
    if (buffered_data_read == 0) {
      break;
    }
    buffered_data_.erase(0, buffered_data_read);
  }
  static constexpr size_t kMaxCapsuleBufferSize = 1024 * 1024;
  if (buffered_data_.size() > kMaxCapsuleBufferSize) {
    buffered_data_.clear();
    ReportParseFailure("Refusing to buffer too much capsule data");
    return false;
  }
  return true;
}

size_t CapsuleParser::AttemptParseCapsule() {
  QUICHE_DCHECK(!parsing_error_occurred_);
  if (buffered_data_.empty()) {
    return 0;
  }
  QuicDataReader capsule_fragment_reader(buffered_data_);
  uint64_t capsule_type64;
  if (!capsule_fragment_reader.ReadVarInt62(&capsule_type64)) {
    QUIC_DVLOG(2) << "Partial read: not enough data to read capsule type";
    return 0;
  }
  absl::string_view capsule_data;
  if (!capsule_fragment_reader.ReadStringPieceVarInt62(&capsule_data)) {
    QUIC_DVLOG(2) << "Partial read: not enough data to read capsule length or "
                     "full capsule data";
    return 0;
  }
  QuicDataReader capsule_data_reader(capsule_data);
  Capsule capsule(static_cast<CapsuleType>(capsule_type64));
  switch (capsule.capsule_type()) {
    case CapsuleType::DATAGRAM:
      capsule.datagram_capsule().http_datagram_payload =
          capsule_data_reader.ReadRemainingPayload();
      break;
    case CapsuleType::LEGACY_DATAGRAM:
      capsule.legacy_datagram_capsule().http_datagram_payload =
          capsule_data_reader.ReadRemainingPayload();
      break;
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT:
      capsule.legacy_datagram_without_context_capsule().http_datagram_payload =
          capsule_data_reader.ReadRemainingPayload();
      break;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      if (!capsule_data_reader.ReadUInt32(
              &capsule.close_web_transport_session_capsule().error_code)) {
        ReportParseFailure(
            "Unable to parse capsule CLOSE_WEBTRANSPORT_SESSION error code");
        return 0;
      }
      capsule.close_web_transport_session_capsule().error_message =
          capsule_data_reader.ReadRemainingPayload();
      break;
    case CapsuleType::ADDRESS_REQUEST: {
      while (!capsule_data_reader.IsDoneReading()) {
        PrefixWithId requested_address;
        if (!capsule_data_reader.ReadVarInt62(&requested_address.request_id)) {
          ReportParseFailure(
              "Unable to parse capsule ADDRESS_REQUEST request ID");
          return 0;
        }
        uint8_t address_family;
        if (!capsule_data_reader.ReadUInt8(&address_family)) {
          ReportParseFailure("Unable to parse capsule ADDRESS_REQUEST family");
          return 0;
        }
        if (address_family != 4 && address_family != 6) {
          ReportParseFailure("Bad ADDRESS_REQUEST family");
          return 0;
        }
        absl::string_view ip_address_bytes;
        if (!capsule_data_reader.ReadStringPiece(
                &ip_address_bytes, address_family == 4
                                       ? QuicIpAddress::kIPv4AddressSize
                                       : QuicIpAddress::kIPv6AddressSize)) {
          ReportParseFailure("Unable to read capsule ADDRESS_REQUEST address");
          return 0;
        }
        quiche::QuicheIpAddress ip_address;
        if (!ip_address.FromPackedString(ip_address_bytes.data(),
                                         ip_address_bytes.size())) {
          ReportParseFailure("Unable to parse capsule ADDRESS_REQUEST address");
          return 0;
        }
        uint8_t ip_prefix_length;
        if (!capsule_data_reader.ReadUInt8(&ip_prefix_length)) {
          ReportParseFailure(
              "Unable to parse capsule ADDRESS_REQUEST IP prefix length");
          return 0;
        }
        if (ip_prefix_length >
            quiche::QuicheIpPrefix(ip_address).prefix_length()) {
          ReportParseFailure("Invalid IP prefix length");
          return 0;
        }
        requested_address.ip_prefix =
            quiche::QuicheIpPrefix(ip_address, ip_prefix_length);
        capsule.address_request_capsule().requested_addresses.push_back(
            requested_address);
      }
    } break;
    case CapsuleType::ADDRESS_ASSIGN: {
      while (!capsule_data_reader.IsDoneReading()) {
        PrefixWithId assigned_address;
        if (!capsule_data_reader.ReadVarInt62(&assigned_address.request_id)) {
          ReportParseFailure(
              "Unable to parse capsule ADDRESS_ASSIGN request ID");
          return 0;
        }
        uint8_t address_family;
        if (!capsule_data_reader.ReadUInt8(&address_family)) {
          ReportParseFailure("Unable to parse capsule ADDRESS_ASSIGN family");
          return 0;
        }
        if (address_family != 4 && address_family != 6) {
          ReportParseFailure("Bad ADDRESS_ASSIGN family");
          return 0;
        }
        absl::string_view ip_address_bytes;
        if (!capsule_data_reader.ReadStringPiece(
                &ip_address_bytes, address_family == 4
                                       ? QuicIpAddress::kIPv4AddressSize
                                       : QuicIpAddress::kIPv6AddressSize)) {
          ReportParseFailure("Unable to read capsule ADDRESS_ASSIGN address");
          return 0;
        }
        quiche::QuicheIpAddress ip_address;
        if (!ip_address.FromPackedString(ip_address_bytes.data(),
                                         ip_address_bytes.size())) {
          ReportParseFailure("Unable to parse capsule ADDRESS_ASSIGN address");
          return 0;
        }
        uint8_t ip_prefix_length;
        if (!capsule_data_reader.ReadUInt8(&ip_prefix_length)) {
          ReportParseFailure(
              "Unable to parse capsule ADDRESS_ASSIGN IP prefix length");
          return 0;
        }
        if (ip_prefix_length >
            quiche::QuicheIpPrefix(ip_address).prefix_length()) {
          ReportParseFailure("Invalid IP prefix length");
          return 0;
        }
        assigned_address.ip_prefix =
            quiche::QuicheIpPrefix(ip_address, ip_prefix_length);
        capsule.address_assign_capsule().assigned_addresses.push_back(
            assigned_address);
      }
    } break;
    case CapsuleType::ROUTE_ADVERTISEMENT: {
      while (!capsule_data_reader.IsDoneReading()) {
        uint8_t address_family;
        if (!capsule_data_reader.ReadUInt8(&address_family)) {
          ReportParseFailure(
              "Unable to parse capsule ROUTE_ADVERTISEMENT family");
          return 0;
        }
        if (address_family != 4 && address_family != 6) {
          ReportParseFailure("Bad ROUTE_ADVERTISEMENT family");
          return 0;
        }
        IpAddressRange ip_address_range;
        absl::string_view start_ip_address_bytes;
        if (!capsule_data_reader.ReadStringPiece(
                &start_ip_address_bytes,
                address_family == 4 ? QuicIpAddress::kIPv4AddressSize
                                    : QuicIpAddress::kIPv6AddressSize)) {
          ReportParseFailure(
              "Unable to read capsule ROUTE_ADVERTISEMENT start address");
          return 0;
        }
        if (!ip_address_range.start_ip_address.FromPackedString(
                start_ip_address_bytes.data(), start_ip_address_bytes.size())) {
          ReportParseFailure(
              "Unable to parse capsule ROUTE_ADVERTISEMENT start address");
          return 0;
        }
        absl::string_view end_ip_address_bytes;
        if (!capsule_data_reader.ReadStringPiece(
                &end_ip_address_bytes, address_family == 4
                                           ? QuicIpAddress::kIPv4AddressSize
                                           : QuicIpAddress::kIPv6AddressSize)) {
          ReportParseFailure(
              "Unable to read capsule ROUTE_ADVERTISEMENT end address");
          return 0;
        }
        if (!ip_address_range.end_ip_address.FromPackedString(
                end_ip_address_bytes.data(), end_ip_address_bytes.size())) {
          ReportParseFailure(
              "Unable to parse capsule ROUTE_ADVERTISEMENT end address");
          return 0;
        }
        if (!capsule_data_reader.ReadUInt8(&ip_address_range.ip_protocol)) {
          ReportParseFailure(
              "Unable to parse capsule ROUTE_ADVERTISEMENT IP protocol");
          return 0;
        }
        capsule.route_advertisement_capsule().ip_address_ranges.push_back(
            ip_address_range);
      }
    } break;
    default:
      capsule.unknown_capsule_data() =
          capsule_data_reader.ReadRemainingPayload();
  }
  if (!visitor_->OnCapsule(capsule)) {
    ReportParseFailure("Visitor failed to process capsule");
    return 0;
  }
  return capsule_fragment_reader.PreviouslyReadPayload().length();
}

void CapsuleParser::ReportParseFailure(const std::string& error_message) {
  if (parsing_error_occurred_) {
    QUIC_BUG(multiple parse errors) << "Experienced multiple parse failures";
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

}  // namespace quic
