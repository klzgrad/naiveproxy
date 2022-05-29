// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_CAPSULE_H_
#define QUICHE_QUIC_CORE_HTTP_CAPSULE_H_

#include <cstdint>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"

namespace quic {

enum class CapsuleType : uint64_t {
  // Casing in this enum matches the IETF specification.
  LEGACY_DATAGRAM = 0xff37a0,  // draft-ietf-masque-h3-datagram-04
  REGISTER_DATAGRAM_CONTEXT = 0xff37a1,
  REGISTER_DATAGRAM_NO_CONTEXT = 0xff37a2,
  CLOSE_DATAGRAM_CONTEXT = 0xff37a3,
  DATAGRAM_WITH_CONTEXT = 0xff37a4,
  DATAGRAM_WITHOUT_CONTEXT = 0xff37a5,
  CLOSE_WEBTRANSPORT_SESSION = 0x2843,
};

QUIC_EXPORT_PRIVATE std::string CapsuleTypeToString(CapsuleType capsule_type);
QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                             const CapsuleType& capsule_type);

enum class DatagramFormatType : uint64_t {
  // Casing in this enum matches the IETF specification.
  UDP_PAYLOAD = 0xff6f00,
  WEBTRANSPORT = 0xff7c00,
};

QUIC_EXPORT_PRIVATE std::string DatagramFormatTypeToString(
    DatagramFormatType datagram_format_type);
QUIC_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, const DatagramFormatType& datagram_format_type);

enum class ContextCloseCode : uint64_t {
  // Casing in this enum matches the IETF specification.
  CLOSE_NO_ERROR = 0xff78a0,  // NO_ERROR already exists in winerror.h.
  UNKNOWN_FORMAT = 0xff78a1,
  DENIED = 0xff78a2,
  RESOURCE_LIMIT = 0xff78a3,
};

QUIC_EXPORT_PRIVATE std::string ContextCloseCodeToString(
    ContextCloseCode context_close_code);
QUIC_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, const ContextCloseCode& context_close_code);

struct QUIC_EXPORT_PRIVATE LegacyDatagramCapsule {
  absl::optional<QuicDatagramContextId> context_id;
  absl::string_view http_datagram_payload;
};
struct QUIC_EXPORT_PRIVATE DatagramWithContextCapsule {
  QuicDatagramContextId context_id;
  absl::string_view http_datagram_payload;
};
struct QUIC_EXPORT_PRIVATE DatagramWithoutContextCapsule {
  absl::string_view http_datagram_payload;
};
struct QUIC_EXPORT_PRIVATE RegisterDatagramContextCapsule {
  QuicDatagramContextId context_id;
  DatagramFormatType format_type;
  absl::string_view format_additional_data;
};
struct QUIC_EXPORT_PRIVATE RegisterDatagramNoContextCapsule {
  DatagramFormatType format_type;
  absl::string_view format_additional_data;
};
struct QUIC_EXPORT_PRIVATE CloseDatagramContextCapsule {
  QuicDatagramContextId context_id;
  ContextCloseCode close_code;
  absl::string_view close_details;
};
struct QUIC_EXPORT_PRIVATE CloseWebTransportSessionCapsule {
  WebTransportSessionError error_code;
  absl::string_view error_message;
};

// Capsule from draft-ietf-masque-h3-datagram.
// IMPORTANT NOTE: Capsule does not own any of the absl::string_view memory it
// points to. Strings saved into a capsule must outlive the capsule object. Any
// code that sees a capsule in a callback needs to either process it immediately
// or perform its own deep copy.
class QUIC_EXPORT_PRIVATE Capsule {
 public:
  static Capsule LegacyDatagram(
      absl::optional<QuicDatagramContextId> context_id = absl::nullopt,
      absl::string_view http_datagram_payload = absl::string_view());
  static Capsule DatagramWithContext(
      QuicDatagramContextId context_id,
      absl::string_view http_datagram_payload = absl::string_view());
  static Capsule DatagramWithoutContext(
      absl::string_view http_datagram_payload = absl::string_view());
  static Capsule RegisterDatagramContext(
      QuicDatagramContextId context_id, DatagramFormatType format_type,
      absl::string_view format_additional_data = absl::string_view());
  static Capsule RegisterDatagramNoContext(
      DatagramFormatType format_type,
      absl::string_view format_additional_data = absl::string_view());
  static Capsule CloseDatagramContext(
      QuicDatagramContextId context_id,
      ContextCloseCode close_code = ContextCloseCode::CLOSE_NO_ERROR,
      absl::string_view close_details = absl::string_view());
  static Capsule CloseWebTransportSession(
      WebTransportSessionError error_code = 0,
      absl::string_view error_message = "");
  static Capsule Unknown(
      uint64_t capsule_type,
      absl::string_view unknown_capsule_data = absl::string_view());

  explicit Capsule(CapsuleType capsule_type);
  Capsule(const Capsule& other);
  Capsule& operator=(const Capsule& other);
  bool operator==(const Capsule& other) const;

  // Human-readable information string for debugging purposes.
  std::string ToString() const;
  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                                      const Capsule& capsule);

  CapsuleType capsule_type() const { return capsule_type_; }
  LegacyDatagramCapsule& legacy_datagram_capsule() {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::LEGACY_DATAGRAM);
    return legacy_datagram_capsule_;
  }
  const LegacyDatagramCapsule& legacy_datagram_capsule() const {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::LEGACY_DATAGRAM);
    return legacy_datagram_capsule_;
  }
  DatagramWithContextCapsule& datagram_with_context_capsule() {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::DATAGRAM_WITH_CONTEXT);
    return datagram_with_context_capsule_;
  }
  const DatagramWithContextCapsule& datagram_with_context_capsule() const {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::DATAGRAM_WITH_CONTEXT);
    return datagram_with_context_capsule_;
  }
  DatagramWithoutContextCapsule& datagram_without_context_capsule() {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::DATAGRAM_WITHOUT_CONTEXT);
    return datagram_without_context_capsule_;
  }
  const DatagramWithoutContextCapsule& datagram_without_context_capsule()
      const {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::DATAGRAM_WITHOUT_CONTEXT);
    return datagram_without_context_capsule_;
  }
  RegisterDatagramContextCapsule& register_datagram_context_capsule() {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::REGISTER_DATAGRAM_CONTEXT);
    return register_datagram_context_capsule_;
  }
  const RegisterDatagramContextCapsule& register_datagram_context_capsule()
      const {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::REGISTER_DATAGRAM_CONTEXT);
    return register_datagram_context_capsule_;
  }
  RegisterDatagramNoContextCapsule& register_datagram_no_context_capsule() {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT);
    return register_datagram_no_context_capsule_;
  }
  const RegisterDatagramNoContextCapsule& register_datagram_no_context_capsule()
      const {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT);
    return register_datagram_no_context_capsule_;
  }
  CloseDatagramContextCapsule& close_datagram_context_capsule() {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::CLOSE_DATAGRAM_CONTEXT);
    return close_datagram_context_capsule_;
  }
  const CloseDatagramContextCapsule& close_datagram_context_capsule() const {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::CLOSE_DATAGRAM_CONTEXT);
    return close_datagram_context_capsule_;
  }
  CloseWebTransportSessionCapsule& close_web_transport_session_capsule() {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::CLOSE_WEBTRANSPORT_SESSION);
    return close_web_transport_session_capsule_;
  }
  const CloseWebTransportSessionCapsule& close_web_transport_session_capsule()
      const {
    QUICHE_DCHECK_EQ(capsule_type_, CapsuleType::CLOSE_WEBTRANSPORT_SESSION);
    return close_web_transport_session_capsule_;
  }
  absl::string_view& unknown_capsule_data() {
    QUICHE_DCHECK(capsule_type_ != CapsuleType::LEGACY_DATAGRAM &&
                  capsule_type_ != CapsuleType::DATAGRAM_WITH_CONTEXT &&
                  capsule_type_ != CapsuleType::DATAGRAM_WITHOUT_CONTEXT &&
                  capsule_type_ != CapsuleType::REGISTER_DATAGRAM_CONTEXT &&
                  capsule_type_ != CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT &&
                  capsule_type_ != CapsuleType::CLOSE_DATAGRAM_CONTEXT &&
                  capsule_type_ != CapsuleType::CLOSE_WEBTRANSPORT_SESSION)
        << capsule_type_;
    return unknown_capsule_data_;
  }
  const absl::string_view& unknown_capsule_data() const {
    QUICHE_DCHECK(capsule_type_ != CapsuleType::LEGACY_DATAGRAM &&
                  capsule_type_ != CapsuleType::DATAGRAM_WITH_CONTEXT &&
                  capsule_type_ != CapsuleType::DATAGRAM_WITHOUT_CONTEXT &&
                  capsule_type_ != CapsuleType::REGISTER_DATAGRAM_CONTEXT &&
                  capsule_type_ != CapsuleType::REGISTER_DATAGRAM_NO_CONTEXT &&
                  capsule_type_ != CapsuleType::CLOSE_DATAGRAM_CONTEXT &&
                  capsule_type_ != CapsuleType::CLOSE_WEBTRANSPORT_SESSION)
        << capsule_type_;
    return unknown_capsule_data_;
  }

 private:
  CapsuleType capsule_type_;
  union {
    LegacyDatagramCapsule legacy_datagram_capsule_;
    DatagramWithContextCapsule datagram_with_context_capsule_;
    DatagramWithoutContextCapsule datagram_without_context_capsule_;
    RegisterDatagramContextCapsule register_datagram_context_capsule_;
    RegisterDatagramNoContextCapsule register_datagram_no_context_capsule_;
    CloseDatagramContextCapsule close_datagram_context_capsule_;
    CloseWebTransportSessionCapsule close_web_transport_session_capsule_;
    absl::string_view unknown_capsule_data_;
  };
};

namespace test {
class CapsuleParserPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE CapsuleParser {
 public:
  class QUIC_EXPORT_PRIVATE Visitor {
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

    virtual void OnCapsuleParseFailure(const std::string& error_message) = 0;
  };

  // |visitor| must be non-null, and must outlive CapsuleParser.
  explicit CapsuleParser(Visitor* visitor);

  void set_datagram_context_id_present(bool datagram_context_id_present) {
    datagram_context_id_present_ = datagram_context_id_present;
  }

  // Ingests a capsule fragment (any fragment of bytes from the capsule data
  // stream) and parses and complete capsules it encounters. Returns false if a
  // parsing error occurred.
  bool IngestCapsuleFragment(absl::string_view capsule_fragment);

  void ErrorIfThereIsRemainingBufferedData();

  friend class test::CapsuleParserPeer;

 private:
  // Attempts to parse a single capsule from |buffered_data_|. If a full capsule
  // is not available, returns 0. If a parsing error occurs, returns 0.
  // Otherwise, returns the number of bytes in the parsed capsule.
  size_t AttemptParseCapsule();
  void ReportParseFailure(const std::string& error_message);

  // Whether HTTP Datagram Context IDs are present.
  bool datagram_context_id_present_ = false;
  // Whether a parsing error has occurred.
  bool parsing_error_occurred_ = false;
  // Visitor which will receive callbacks, unowned.
  Visitor* visitor_;

  std::string buffered_data_;
};

// Serializes |capsule| into a newly allocated buffer.
QUIC_EXPORT_PRIVATE quiche::QuicheBuffer SerializeCapsule(
    const Capsule& capsule, quiche::QuicheBufferAllocator* allocator);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_CAPSULE_H_
