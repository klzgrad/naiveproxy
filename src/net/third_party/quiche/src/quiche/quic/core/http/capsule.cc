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
    case CapsuleType::REGISTER_DATAGRAM_CONTEXT:
      return "REGISTER_DATAGRAM_CONTEXT";
    case CapsuleType::CLOSE_DATAGRAM_CONTEXT:
      return "CLOSE_DATAGRAM_CONTEXT";
    case CapsuleType::LEGACY_DATAGRAM:
      return "LEGACY_DATAGRAM";
    case CapsuleType::DATAGRAM_WITH_CONTEXT:
      return "DATAGRAM_WITH_CONTEXT";
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      return "DATAGRAM_WITHOUT_CONTEXT";
    case CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT:
      return "REGISTER_DATAGRAM_NO_CONTEXT";
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      return "CLOSE_WEBTRANSPORT_SESSION";
  }
  return absl::StrCat("Unknown(", static_cast<uint64_t>(capsule_type), ")");
}

std::ostream& operator<<(std::ostream& os, const CapsuleType& capsule_type) {
  os << CapsuleTypeToString(capsule_type);
  return os;
}

std::string DatagramFormatTypeToString(
    DatagramFormatType datagram_format_type) {
  switch (datagram_format_type) {
    case DatagramFormatType::UDP_PAYLOAD:
      return "UDP_PAYLOAD";
    case DatagramFormatType::WEBTRANSPORT:
      return "WEBTRANSPORT";
  }
  return absl::StrCat("Unknown(", static_cast<uint64_t>(datagram_format_type),
                      ")");
}

std::ostream& operator<<(std::ostream& os,
                         const DatagramFormatType& datagram_format_type) {
  os << DatagramFormatTypeToString(datagram_format_type);
  return os;
}

std::string ContextCloseCodeToString(ContextCloseCode context_close_code) {
  switch (context_close_code) {
    case ContextCloseCode::CLOSE_NO_ERROR:
      return "NO_ERROR";
    case ContextCloseCode::UNKNOWN_FORMAT:
      return "UNKNOWN_FORMAT";
    case ContextCloseCode::DENIED:
      return "DENIED";
    case ContextCloseCode::RESOURCE_LIMIT:
      return "RESOURCE_LIMIT";
  }
  return absl::StrCat("Unknown(", static_cast<uint64_t>(context_close_code),
                      ")");
}

std::ostream& operator<<(std::ostream& os,
                         const ContextCloseCode& context_close_code) {
  os << ContextCloseCodeToString(context_close_code);
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
    case CapsuleType::DATAGRAM_WITH_CONTEXT:
      static_assert(
          std::is_standard_layout<DatagramWithContextCapsule>::value &&
              std::is_trivially_destructible<DatagramWithContextCapsule>::value,
          "All capsule structs must have these properties");
      datagram_with_context_capsule_ = DatagramWithContextCapsule();
      break;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      static_assert(
          std::is_standard_layout<DatagramWithoutContextCapsule>::value &&
              std::is_trivially_destructible<
                  DatagramWithoutContextCapsule>::value,
          "All capsule structs must have these properties");
      datagram_without_context_capsule_ = DatagramWithoutContextCapsule();
      break;
    case CapsuleType::REGISTER_DATAGRAM_CONTEXT:
      static_assert(
          std::is_standard_layout<RegisterDatagramContextCapsule>::value &&
              std::is_trivially_destructible<
                  RegisterDatagramContextCapsule>::value,
          "All capsule structs must have these properties");
      register_datagram_context_capsule_ = RegisterDatagramContextCapsule();
      break;
    case CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT:
      static_assert(
          std::is_standard_layout<RegisterDatagramNoContextCapsule>::value &&
              std::is_trivially_destructible<
                  RegisterDatagramNoContextCapsule>::value,
          "All capsule structs must have these properties");
      register_datagram_no_context_capsule_ =
          RegisterDatagramNoContextCapsule();
      break;
    case CapsuleType::CLOSE_DATAGRAM_CONTEXT:
      static_assert(
          std::is_standard_layout<CloseDatagramContextCapsule>::value &&
              std::is_trivially_destructible<
                  CloseDatagramContextCapsule>::value,
          "All capsule structs must have these properties");
      close_datagram_context_capsule_ = CloseDatagramContextCapsule();
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
    absl::optional<QuicDatagramContextId> context_id,
    absl::string_view http_datagram_payload) {
  Capsule capsule(CapsuleType::LEGACY_DATAGRAM);
  capsule.legacy_datagram_capsule().context_id = context_id;
  capsule.legacy_datagram_capsule().http_datagram_payload =
      http_datagram_payload;
  return capsule;
}

// static
Capsule Capsule::DatagramWithContext(QuicDatagramContextId context_id,
                                     absl::string_view http_datagram_payload) {
  Capsule capsule(CapsuleType::DATAGRAM_WITH_CONTEXT);
  capsule.datagram_with_context_capsule().context_id = context_id;
  capsule.datagram_with_context_capsule().http_datagram_payload =
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
Capsule Capsule::RegisterDatagramContext(
    QuicDatagramContextId context_id, DatagramFormatType format_type,
    absl::string_view format_additional_data) {
  Capsule capsule(CapsuleType::REGISTER_DATAGRAM_CONTEXT);
  capsule.register_datagram_context_capsule().context_id = context_id;
  capsule.register_datagram_context_capsule().format_type = format_type;
  capsule.register_datagram_context_capsule().format_additional_data =
      format_additional_data;
  return capsule;
}

// static
Capsule Capsule::RegisterDatagramNoContext(
    DatagramFormatType format_type, absl::string_view format_additional_data) {
  Capsule capsule(CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT);
  capsule.register_datagram_no_context_capsule().format_type = format_type;
  capsule.register_datagram_no_context_capsule().format_additional_data =
      format_additional_data;
  return capsule;
}

// static
Capsule Capsule::CloseDatagramContext(QuicDatagramContextId context_id,
                                      ContextCloseCode close_code,
                                      absl::string_view close_details) {
  Capsule capsule(CapsuleType::CLOSE_DATAGRAM_CONTEXT);
  capsule.close_datagram_context_capsule().context_id = context_id;
  capsule.close_datagram_context_capsule().close_code = close_code;
  capsule.close_datagram_context_capsule().close_details = close_details;
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
    case CapsuleType::DATAGRAM_WITH_CONTEXT:
      datagram_with_context_capsule_ = other.datagram_with_context_capsule_;
      break;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      datagram_without_context_capsule_ =
          other.datagram_without_context_capsule_;
      break;
    case CapsuleType::REGISTER_DATAGRAM_CONTEXT:
      register_datagram_context_capsule_ =
          other.register_datagram_context_capsule_;
      break;
    case CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT:
      register_datagram_no_context_capsule_ =
          other.register_datagram_no_context_capsule_;
      break;
    case CapsuleType::CLOSE_DATAGRAM_CONTEXT:
      close_datagram_context_capsule_ = other.close_datagram_context_capsule_;
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
      return legacy_datagram_capsule_.context_id ==
                 other.legacy_datagram_capsule_.context_id &&
             legacy_datagram_capsule_.http_datagram_payload ==
                 other.legacy_datagram_capsule_.http_datagram_payload;
    case CapsuleType::DATAGRAM_WITH_CONTEXT:
      return datagram_with_context_capsule_.context_id ==
                 other.datagram_with_context_capsule_.context_id &&
             datagram_with_context_capsule_.http_datagram_payload ==
                 other.datagram_with_context_capsule_.http_datagram_payload;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      return datagram_without_context_capsule_.http_datagram_payload ==
             other.datagram_without_context_capsule_.http_datagram_payload;
    case CapsuleType::REGISTER_DATAGRAM_CONTEXT:
      return register_datagram_context_capsule_.context_id ==
                 other.register_datagram_context_capsule_.context_id &&
             register_datagram_context_capsule_.format_type ==
                 other.register_datagram_context_capsule_.format_type &&
             register_datagram_context_capsule_.format_additional_data ==
                 other.register_datagram_context_capsule_
                     .format_additional_data;
    case CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT:
      return register_datagram_no_context_capsule_.format_type ==
                 other.register_datagram_no_context_capsule_.format_type &&
             register_datagram_no_context_capsule_.format_additional_data ==
                 other.register_datagram_no_context_capsule_
                     .format_additional_data;
    case CapsuleType::CLOSE_DATAGRAM_CONTEXT:
      return close_datagram_context_capsule_.context_id ==
                 other.close_datagram_context_capsule_.context_id &&
             close_datagram_context_capsule_.close_code ==
                 other.close_datagram_context_capsule_.close_code &&
             close_datagram_context_capsule_.close_details ==
                 other.close_datagram_context_capsule_.close_details;
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
      if (legacy_datagram_capsule_.context_id.has_value()) {
        absl::StrAppend(&rv, "(", legacy_datagram_capsule_.context_id.value(),
                        ")");
      }
      absl::StrAppend(&rv, "[",
                      absl::BytesToHexString(
                          legacy_datagram_capsule_.http_datagram_payload),
                      "]");
      break;
    case CapsuleType::DATAGRAM_WITH_CONTEXT:
      absl::StrAppend(&rv, "(", datagram_with_context_capsule_.context_id, ")[",
                      absl::BytesToHexString(
                          datagram_with_context_capsule_.http_datagram_payload),
                      "]");
      break;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      absl::StrAppend(
          &rv, "[",
          absl::BytesToHexString(
              datagram_without_context_capsule_.http_datagram_payload),
          "]");
      break;
    case CapsuleType::REGISTER_DATAGRAM_CONTEXT:
      absl::StrAppend(
          &rv, "(context_id=", register_datagram_context_capsule_.context_id,
          ",format_type=",
          DatagramFormatTypeToString(
              register_datagram_context_capsule_.format_type),
          "){",
          absl::BytesToHexString(
              register_datagram_context_capsule_.format_additional_data),
          "}");
      break;
    case CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT:
      absl::StrAppend(
          &rv, "(format_type=",
          DatagramFormatTypeToString(
              register_datagram_no_context_capsule_.format_type),
          "){",
          absl::BytesToHexString(
              register_datagram_no_context_capsule_.format_additional_data),
          "}");
      break;
    case CapsuleType::CLOSE_DATAGRAM_CONTEXT:
      absl::StrAppend(
          &rv, "(context_id=", close_datagram_context_capsule_.context_id,
          ",close_code=",
          ContextCloseCodeToString(close_datagram_context_capsule_.close_code),
          ",close_details=\"",
          absl::BytesToHexString(close_datagram_context_capsule_.close_details),
          "\")");
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
      if (capsule.legacy_datagram_capsule().context_id.has_value()) {
        capsule_data_length += QuicDataWriter::GetVarInt62Len(
            capsule.legacy_datagram_capsule().context_id.value());
      }
      break;
    case CapsuleType::DATAGRAM_WITH_CONTEXT:
      capsule_data_length =
          QuicDataWriter::GetVarInt62Len(
              capsule.datagram_with_context_capsule().context_id) +
          capsule.datagram_with_context_capsule()
              .http_datagram_payload.length();
      break;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      capsule_data_length = capsule.datagram_without_context_capsule()
                                .http_datagram_payload.length();
      break;
    case CapsuleType::REGISTER_DATAGRAM_CONTEXT:
      capsule_data_length =
          QuicDataWriter::GetVarInt62Len(
              capsule.register_datagram_context_capsule().context_id) +
          QuicDataWriter::GetVarInt62Len(static_cast<uint64_t>(
              capsule.register_datagram_context_capsule().format_type)) +
          capsule.register_datagram_context_capsule()
              .format_additional_data.length();
      break;
    case CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT:
      capsule_data_length =
          QuicDataWriter::GetVarInt62Len(static_cast<uint64_t>(
              capsule.register_datagram_no_context_capsule().format_type)) +
          capsule.register_datagram_no_context_capsule()
              .format_additional_data.length();
      break;
    case CapsuleType::CLOSE_DATAGRAM_CONTEXT:
      capsule_data_length =
          QuicDataWriter::GetVarInt62Len(
              capsule.close_datagram_context_capsule().context_id) +
          QuicDataWriter::GetVarInt62Len(static_cast<uint64_t>(
              capsule.close_datagram_context_capsule().close_code)) +
          capsule.close_datagram_context_capsule().close_details.length();
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
      if (capsule.legacy_datagram_capsule().context_id.has_value()) {
        if (!writer.WriteVarInt62(
                capsule.legacy_datagram_capsule().context_id.value())) {
          QUIC_BUG(datagram capsule context ID write fail)
              << "Failed to write LEGACY_DATAGRAM CAPSULE context ID";
          return {};
        }
      }
      if (!writer.WriteStringPiece(
              capsule.legacy_datagram_capsule().http_datagram_payload)) {
        QUIC_BUG(datagram capsule payload write fail)
            << "Failed to write LEGACY_DATAGRAM CAPSULE payload";
        return {};
      }
      break;
    case CapsuleType::DATAGRAM_WITH_CONTEXT:
      if (!writer.WriteVarInt62(
              capsule.datagram_with_context_capsule().context_id)) {
        QUIC_BUG(datagram capsule context ID write fail)
            << "Failed to write DATAGRAM_WITH_CONTEXT CAPSULE context ID";
        return {};
      }
      if (!writer.WriteStringPiece(
              capsule.datagram_with_context_capsule().http_datagram_payload)) {
        QUIC_BUG(datagram capsule payload write fail)
            << "Failed to write DATAGRAM_WITH_CONTEXT CAPSULE payload";
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
    case CapsuleType::REGISTER_DATAGRAM_CONTEXT:
      if (!writer.WriteVarInt62(
              capsule.register_datagram_context_capsule().context_id)) {
        QUIC_BUG(register context capsule context ID write fail)
            << "Failed to write REGISTER_DATAGRAM_CONTEXT CAPSULE context ID";
        return {};
      }
      if (!writer.WriteVarInt62(static_cast<uint64_t>(
              capsule.register_datagram_context_capsule().format_type))) {
        QUIC_BUG(register context capsule format type write fail)
            << "Failed to write REGISTER_DATAGRAM_CONTEXT CAPSULE format type";
        return {};
      }
      if (!writer.WriteStringPiece(capsule.register_datagram_context_capsule()
                                       .format_additional_data)) {
        QUIC_BUG(register context capsule additional data write fail)
            << "Failed to write REGISTER_DATAGRAM_CONTEXT CAPSULE additional "
               "data";
        return {};
      }
      break;
    case CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT:
      if (!writer.WriteVarInt62(static_cast<uint64_t>(
              capsule.register_datagram_no_context_capsule().format_type))) {
        QUIC_BUG(register no context capsule format type write fail)
            << "Failed to write REGISTER_DATAGRAM_NO_CONTEXT CAPSULE format "
               "type";
        return {};
      }
      if (!writer.WriteStringPiece(
              capsule.register_datagram_no_context_capsule()
                  .format_additional_data)) {
        QUIC_BUG(register no context capsule additional data write fail)
            << "Failed to write REGISTER_DATAGRAM_NO_CONTEXT CAPSULE "
               "additional data";
        return {};
      }
      break;
    case CapsuleType::CLOSE_DATAGRAM_CONTEXT:
      if (!writer.WriteVarInt62(
              capsule.close_datagram_context_capsule().context_id)) {
        QUIC_BUG(close context capsule context ID write fail)
            << "Failed to write CLOSE_DATAGRAM_CONTEXT CAPSULE context ID";
        return {};
      }
      if (!writer.WriteVarInt62(static_cast<uint64_t>(
              capsule.close_datagram_context_capsule().close_code))) {
        QUIC_BUG(close context capsule close code write fail)
            << "Failed to write CLOSE_DATAGRAM_CONTEXT CAPSULE close code";
        return {};
      }
      if (!writer.WriteStringPiece(
              capsule.close_datagram_context_capsule().close_details)) {
        QUIC_BUG(close context capsule close details write fail)
            << "Failed to write CLOSE_DATAGRAM_CONTEXT CAPSULE close details";
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
      if (datagram_context_id_present_) {
        uint64_t context_id;
        if (!capsule_data_reader.ReadVarInt62(&context_id)) {
          ReportParseFailure(
              "Unable to parse capsule LEGACY_DATAGRAM context ID");
          return 0;
        }
        capsule.legacy_datagram_capsule().context_id = context_id;
      }
      capsule.legacy_datagram_capsule().http_datagram_payload =
          capsule_data_reader.ReadRemainingPayload();
      break;
    case CapsuleType::DATAGRAM_WITH_CONTEXT:
      uint64_t context_id;
      if (!capsule_data_reader.ReadVarInt62(&context_id)) {
        ReportParseFailure(
            "Unable to parse capsule DATAGRAM_WITH_CONTEXT context ID");
        return 0;
      }
      capsule.datagram_with_context_capsule().context_id = context_id;
      capsule.datagram_with_context_capsule().http_datagram_payload =
          capsule_data_reader.ReadRemainingPayload();
      break;
    case CapsuleType::DATAGRAM_WITHOUT_CONTEXT:
      capsule.datagram_without_context_capsule().http_datagram_payload =
          capsule_data_reader.ReadRemainingPayload();
      break;
    case CapsuleType::REGISTER_DATAGRAM_CONTEXT:
      if (!capsule_data_reader.ReadVarInt62(
              &capsule.register_datagram_context_capsule().context_id)) {
        ReportParseFailure(
            "Unable to parse capsule REGISTER_DATAGRAM_CONTEXT context ID");
        return 0;
      }
      if (!capsule_data_reader.ReadVarInt62(reinterpret_cast<uint64_t*>(
              &capsule.register_datagram_context_capsule().format_type))) {
        ReportParseFailure(
            "Unable to parse capsule REGISTER_DATAGRAM_CONTEXT format type");
        return 0;
      }
      capsule.register_datagram_context_capsule().format_additional_data =
          capsule_data_reader.ReadRemainingPayload();
      break;
    case CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT:
      if (!capsule_data_reader.ReadVarInt62(reinterpret_cast<uint64_t*>(
              &capsule.register_datagram_no_context_capsule().format_type))) {
        ReportParseFailure(
            "Unable to parse capsule REGISTER_DATAGRAM_NO_CONTEXT format type");
        return 0;
      }
      capsule.register_datagram_no_context_capsule().format_additional_data =
          capsule_data_reader.ReadRemainingPayload();
      break;
    case CapsuleType::CLOSE_DATAGRAM_CONTEXT:
      if (!capsule_data_reader.ReadVarInt62(
              &capsule.close_datagram_context_capsule().context_id)) {
        ReportParseFailure(
            "Unable to parse capsule CLOSE_DATAGRAM_CONTEXT context ID");
        return 0;
      }
      if (!capsule_data_reader.ReadVarInt62(reinterpret_cast<uint64_t*>(
              &capsule.close_datagram_context_capsule().close_code))) {
        ReportParseFailure(
            "Unable to parse capsule CLOSE_DATAGRAM_CONTEXT close code");
        return 0;
      }
      capsule.close_datagram_context_capsule().close_details =
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
