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
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

std::string CapsuleTypeToString(CapsuleType capsule_type) {
  switch (capsule_type) {
    case CapsuleType::LEGACY_DATAGRAM:
      return "LEGACY_DATAGRAM";
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      return "DATAGRAM_WITHOUT_CONTEXT";
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      return "CLOSE_WEBTRANSPORT_SESSION";
  }
  return absl::StrCat("Unknown(", static_cast<uint64_t>(capsule_type), ")");
}

std::ostream& operator<<(std::ostream& os, const CapsuleType& capsule_type) {
  os << CapsuleTypeToString(capsule_type);
  return os;
}

Capsule::Capsule(CapsuleType capsule_type) : capsule_type_(capsule_type) {
  switch (capsule_type) {
    case CapsuleType::LEGACY_DATAGRAM:
      static_assert(
          std::is_standard_layout<LegacyDatagramCapsule>::value &&
              std::is_trivially_destructible<LegacyDatagramCapsule>::value,
          "All capsule structs must have these properties");
      legacy_datagram_capsule_ = LegacyDatagramCapsule();
      break;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      static_assert(
          std::is_standard_layout<DatagramWithoutContextCapsule>::value &&
              std::is_trivially_destructible<
                  DatagramWithoutContextCapsule>::value,
          "All capsule structs must have these properties");
      datagram_without_context_capsule_ = DatagramWithoutContextCapsule();
      break;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      static_assert(
          std::is_standard_layout<CloseWebTransportSessionCapsule>::value &&
              std::is_trivially_destructible<
                  CloseWebTransportSessionCapsule>::value,
          "All capsule structs must have these properties");
      close_web_transport_session_capsule_ = CloseWebTransportSessionCapsule();
      break;
    default:
      unknown_capsule_data_ = absl::string_view();
      break;
  }
}

// static
Capsule Capsule::LegacyDatagram(
    absl::string_view http_datagram_payload) {
  Capsule capsule(CapsuleType::LEGACY_DATAGRAM);
  capsule.legacy_datagram_capsule().http_datagram_payload =
      http_datagram_payload;
  return capsule;
}

// static
Capsule Capsule::DatagramWithoutContext(
    absl::string_view http_datagram_payload) {
  Capsule capsule(CapsuleType::DATAGRAM_WITHOUT_CONTEXT);
  capsule.datagram_without_context_capsule().http_datagram_payload =
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
Capsule Capsule::Unknown(uint64_t capsule_type,
                         absl::string_view unknown_capsule_data) {
  Capsule capsule(static_cast<CapsuleType>(capsule_type));
  capsule.unknown_capsule_data() = unknown_capsule_data;
  return capsule;
}

Capsule& Capsule::operator=(const Capsule& other) {
  capsule_type_ = other.capsule_type_;
  switch (capsule_type_) {
    case CapsuleType::LEGACY_DATAGRAM:
      legacy_datagram_capsule_ = other.legacy_datagram_capsule_;
      break;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      datagram_without_context_capsule_ =
          other.datagram_without_context_capsule_;
      break;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      close_web_transport_session_capsule_ =
          other.close_web_transport_session_capsule_;
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
    case CapsuleType::LEGACY_DATAGRAM:
      return legacy_datagram_capsule_.http_datagram_payload ==
             other.legacy_datagram_capsule_.http_datagram_payload;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      return datagram_without_context_capsule_.http_datagram_payload ==
             other.datagram_without_context_capsule_.http_datagram_payload;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      return close_web_transport_session_capsule_.error_code ==
                 other.close_web_transport_session_capsule_.error_code &&
             close_web_transport_session_capsule_.error_message ==
                 other.close_web_transport_session_capsule_.error_message;
    default:
      return unknown_capsule_data_ == other.unknown_capsule_data_;
  }
}

std::string Capsule::ToString() const {
  std::string rv = CapsuleTypeToString(capsule_type_);
  switch (capsule_type_) {
    case CapsuleType::LEGACY_DATAGRAM:
      absl::StrAppend(&rv, "[",
                      absl::BytesToHexString(
                          legacy_datagram_capsule_.http_datagram_payload),
                      "]");
      break;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      absl::StrAppend(
          &rv, "[",
          absl::BytesToHexString(
              datagram_without_context_capsule_.http_datagram_payload),
          "]");
      break;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      absl::StrAppend(
          &rv, "(error_code=", close_web_transport_session_capsule_.error_code,
          ",error_message=\"",
          close_web_transport_session_capsule_.error_message, "\")");
      break;
    default:
      absl::StrAppend(&rv, "[", absl::BytesToHexString(unknown_capsule_data_),
                      "]");
      break;
  }
  return rv;
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
    case CapsuleType::LEGACY_DATAGRAM:
      capsule_data_length =
          capsule.legacy_datagram_capsule().http_datagram_payload.length();
      break;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      capsule_data_length = capsule.datagram_without_context_capsule()
                                .http_datagram_payload.length();
      break;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      capsule_data_length =
          sizeof(WebTransportSessionError) +
          capsule.close_web_transport_session_capsule().error_message.size();
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
    case CapsuleType::LEGACY_DATAGRAM:
      if (!writer.WriteStringPiece(
              capsule.legacy_datagram_capsule().http_datagram_payload)) {
        QUIC_BUG(datagram capsule payload write fail)
            << "Failed to write LEGACY_DATAGRAM CAPSULE payload";
        return {};
      }
      break;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      if (!writer.WriteStringPiece(capsule.datagram_without_context_capsule()
                                       .http_datagram_payload)) {
        QUIC_BUG(datagram capsule payload write fail)
            << "Failed to write DATAGRAM_WITHOUT_CONTEXT CAPSULE payload";
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
    case CapsuleType::LEGACY_DATAGRAM:
      capsule.legacy_datagram_capsule().http_datagram_payload =
          capsule_data_reader.ReadRemainingPayload();
      break;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      capsule.datagram_without_context_capsule().http_datagram_payload =
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

}  // namespace quic
