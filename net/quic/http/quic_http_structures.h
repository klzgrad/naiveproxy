// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_QUIC_HTTP_STRUCTURES_H_
#define NET_QUIC_HTTP_QUIC_HTTP_STRUCTURES_H_

// Defines structs for various fixed sized structures in HTTP/2.
//
// Those structs with multiple fields have constructors that take arguments in
// the same order as their encoding (which may be different from their order
// in the struct). For single field structs, use aggregate initialization if
// desired, e.g.:
//
//   QuicHttpRstStreamFields var{QuicHttpErrorCode::ENHANCE_YOUR_CALM};
// or:
//   SomeFunc(QuicHttpRstStreamFields{QuicHttpErrorCode::ENHANCE_YOUR_CALM});
//
// Each struct includes a static method EncodedSize which returns the number
// of bytes of the encoding.
//
// With the exception of QuicHttpFrameHeader, all the types are named
// QuicHttp<X>Fields, where X is the title-case form of the frame which always
// includes the fields; the "always" is to cover the case of the
// QUIC_HTTP_PRIORITY frame; its fields optionally appear in the HEADERS frame,
// but the struct is called QuicHttpPriorityFields.

#include <stddef.h>

#include <cstdint>

#include "base/logging.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string.h"

namespace net {

struct QUIC_EXPORT_PRIVATE QuicHttpFrameHeader {
  QuicHttpFrameHeader() {}
  QuicHttpFrameHeader(uint32_t payload_length,
                      QuicHttpFrameType type,
                      uint8_t flags,
                      uint32_t stream_id)
      : payload_length(payload_length),
        stream_id(stream_id),
        type(type),
        flags(static_cast<QuicHttpFrameFlag>(flags)) {
    DCHECK_LT(payload_length, 1u << 24)
        << "Payload Length is only a 24 bit field\n"
        << ToString();
  }

  static constexpr size_t EncodedSize() { return 9; }

  // Keep the current value of those flags that are in
  // valid_flags, and clear all the others.
  void RetainFlags(uint8_t valid_flags) {
    flags = static_cast<QuicHttpFrameFlag>(flags & valid_flags);
  }

  // Returns true if any of the flags in flag_mask are set,
  // otherwise false.
  bool HasAnyFlags(uint8_t flag_mask) const { return 0 != (flags & flag_mask); }

  // Is the QUIC_HTTP_END_STREAM flag set?
  bool IsEndStream() const {
    DCHECK(type == QuicHttpFrameType::DATA ||
           type == QuicHttpFrameType::HEADERS)
        << ToString();
    return (flags & QuicHttpFrameFlag::QUIC_HTTP_END_STREAM) != 0;
  }

  // Is the QUIC_HTTP_ACK flag set?
  bool IsAck() const {
    DCHECK(type == QuicHttpFrameType::SETTINGS ||
           type == QuicHttpFrameType::PING)
        << ToString();
    return (flags & QuicHttpFrameFlag::QUIC_HTTP_ACK) != 0;
  }

  // Is the QUIC_HTTP_END_HEADERS flag set?
  bool IsEndHeaders() const {
    DCHECK(type == QuicHttpFrameType::HEADERS ||
           type == QuicHttpFrameType::PUSH_PROMISE ||
           type == QuicHttpFrameType::CONTINUATION)
        << ToString();
    return (flags & QuicHttpFrameFlag::QUIC_HTTP_END_HEADERS) != 0;
  }

  // Is the QUIC_HTTP_PADDED flag set?
  bool IsPadded() const {
    DCHECK(type == QuicHttpFrameType::DATA ||
           type == QuicHttpFrameType::HEADERS ||
           type == QuicHttpFrameType::PUSH_PROMISE)
        << ToString();
    return (flags & QuicHttpFrameFlag::QUIC_HTTP_PADDED) != 0;
  }

  // Is the QUIC_HTTP_PRIORITY flag set?
  bool HasPriority() const {
    DCHECK_EQ(type, QuicHttpFrameType::HEADERS) << ToString();
    return (flags & QuicHttpFrameFlag::QUIC_HTTP_PRIORITY) != 0;
  }

  // Does the encoding of this header start with "HTTP/", indicating that it
  // might be from a non-HTTP/2 server.
  bool IsProbableHttpResponse() const;

  // Produce std::strings useful for debugging/logging messages.
  QuicString ToString() const;
  QuicString FlagsToString() const;

  // 24 bit length of the payload after the header, including any padding.
  // First field in encoding.
  uint32_t payload_length;  // 24 bits

  // 31 bit stream id, with high bit (32nd bit) reserved (must be zero),
  // and is cleared during decoding.
  // Fourth field in encoding.
  uint32_t stream_id;

  // Type of the frame.
  // Second field in encoding.
  QuicHttpFrameType type;

  // Flag bits, with interpretations that depend upon the frame type.
  // Flag bits not used by the frame type are cleared.
  // Third field in encoding.
  QuicHttpFrameFlag flags;
};

QUIC_EXPORT_PRIVATE bool operator==(const QuicHttpFrameHeader& a,
                                    const QuicHttpFrameHeader& b);
QUIC_EXPORT_PRIVATE inline bool operator!=(const QuicHttpFrameHeader& a,
                                           const QuicHttpFrameHeader& b) {
  return !(a == b);
}
QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                             const QuicHttpFrameHeader& v);

// QuicHttpPriorityFields:

struct QUIC_EXPORT_PRIVATE QuicHttpPriorityFields {
  QuicHttpPriorityFields() {}
  QuicHttpPriorityFields(uint32_t stream_dependency,
                         uint32_t weight,
                         bool is_exclusive)
      : stream_dependency(stream_dependency),
        weight(weight),
        is_exclusive(is_exclusive) {
    // Can't have the high-bit set in the stream id because we need to use
    // that for the EXCLUSIVE flag bit.
    DCHECK_EQ(stream_dependency, stream_dependency & QuicHttpStreamIdMask())
        << "Stream Dependency is only a 31-bit field.\n"
        << ToString();
    DCHECK_LE(1u, weight) << "Weight is too small.";
    DCHECK_LE(weight, 256u) << "Weight is too large.";
  }
  static constexpr size_t EncodedSize() { return 5; }

  QuicString ToString() const;

  // A 31-bit stream identifier for the stream that this stream depends on.
  uint32_t stream_dependency;

  // Weight (1 to 256) is encoded as a byte in the range 0 to 255, so we
  // add one when decoding, and store it in a field larger than a byte.
  uint32_t weight;

  // A single-bit flag indicating that the stream dependency is exclusive;
  // extracted from high bit of stream dependency field during decoding.
  bool is_exclusive;
};

QUIC_EXPORT_PRIVATE bool operator==(const QuicHttpPriorityFields& a,
                                    const QuicHttpPriorityFields& b);
QUIC_EXPORT_PRIVATE inline bool operator!=(const QuicHttpPriorityFields& a,
                                           const QuicHttpPriorityFields& b) {
  return !(a == b);
}
QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                             const QuicHttpPriorityFields& v);

// QuicHttpRstStreamFields:

struct QuicHttpRstStreamFields {
  static constexpr size_t EncodedSize() { return 4; }
  bool IsSupportedErrorCode() const {
    return IsSupportedQuicHttpErrorCode(error_code);
  }

  QuicHttpErrorCode error_code;
};

QUIC_EXPORT_PRIVATE bool operator==(const QuicHttpRstStreamFields& a,
                                    const QuicHttpRstStreamFields& b);
QUIC_EXPORT_PRIVATE inline bool operator!=(const QuicHttpRstStreamFields& a,
                                           const QuicHttpRstStreamFields& b) {
  return !(a == b);
}
QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                             const QuicHttpRstStreamFields& v);

// QuicHttpSettingFields:

struct QuicHttpSettingFields {
  QuicHttpSettingFields() {}
  QuicHttpSettingFields(QuicHttpSettingsParameter parameter, uint32_t value)
      : parameter(parameter), value(value) {}
  static constexpr size_t EncodedSize() { return 6; }
  bool IsSupportedParameter() const {
    return IsSupportedQuicHttpSettingsParameter(parameter);
  }

  QuicHttpSettingsParameter parameter;
  uint32_t value;
};

QUIC_EXPORT_PRIVATE bool operator==(const QuicHttpSettingFields& a,
                                    const QuicHttpSettingFields& b);
QUIC_EXPORT_PRIVATE inline bool operator!=(const QuicHttpSettingFields& a,
                                           const QuicHttpSettingFields& b) {
  return !(a == b);
}
QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                             const QuicHttpSettingFields& v);

// QuicHttpPushPromiseFields:

struct QuicHttpPushPromiseFields {
  static constexpr size_t EncodedSize() { return 4; }

  uint32_t promised_stream_id;
};

QUIC_EXPORT_PRIVATE bool operator==(const QuicHttpPushPromiseFields& a,
                                    const QuicHttpPushPromiseFields& b);
QUIC_EXPORT_PRIVATE inline bool operator!=(const QuicHttpPushPromiseFields& a,
                                           const QuicHttpPushPromiseFields& b) {
  return !(a == b);
}
QUIC_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& out,
    const QuicHttpPushPromiseFields& v);

// QuicHttpPingFields:

struct QuicHttpPingFields {
  static constexpr size_t EncodedSize() { return 8; }

  // TODO(jamessynge): Rename opaque_bytes to opaque_bytes.
  uint8_t opaque_bytes[8];
};

QUIC_EXPORT_PRIVATE bool operator==(const QuicHttpPingFields& a,
                                    const QuicHttpPingFields& b);
QUIC_EXPORT_PRIVATE inline bool operator!=(const QuicHttpPingFields& a,
                                           const QuicHttpPingFields& b) {
  return !(a == b);
}
QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                             const QuicHttpPingFields& v);

// QuicHttpGoAwayFields:

struct QuicHttpGoAwayFields {
  QuicHttpGoAwayFields() {}
  QuicHttpGoAwayFields(uint32_t last_stream_id, QuicHttpErrorCode error_code)
      : last_stream_id(last_stream_id), error_code(error_code) {}
  static constexpr size_t EncodedSize() { return 8; }
  bool IsSupportedErrorCode() const {
    return IsSupportedQuicHttpErrorCode(error_code);
  }

  uint32_t last_stream_id;
  QuicHttpErrorCode error_code;
};

QUIC_EXPORT_PRIVATE bool operator==(const QuicHttpGoAwayFields& a,
                                    const QuicHttpGoAwayFields& b);
QUIC_EXPORT_PRIVATE inline bool operator!=(const QuicHttpGoAwayFields& a,
                                           const QuicHttpGoAwayFields& b) {
  return !(a == b);
}
QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                             const QuicHttpGoAwayFields& v);

// QuicHttpWindowUpdateFields:

struct QuicHttpWindowUpdateFields {
  static constexpr size_t EncodedSize() { return 4; }

  // 31-bit, unsigned increase in the window size (only positive values are
  // allowed). The high-bit is reserved for the future.
  uint32_t window_size_increment;
};

QUIC_EXPORT_PRIVATE bool operator==(const QuicHttpWindowUpdateFields& a,
                                    const QuicHttpWindowUpdateFields& b);
QUIC_EXPORT_PRIVATE inline bool operator!=(
    const QuicHttpWindowUpdateFields& a,
    const QuicHttpWindowUpdateFields& b) {
  return !(a == b);
}
QUIC_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& out,
    const QuicHttpWindowUpdateFields& v);

// QuicHttpAltSvcFields:

struct QuicHttpAltSvcFields {
  static constexpr size_t EncodedSize() { return 2; }

  // This is the one fixed size portion of the ALTSVC payload.
  uint16_t origin_length;
};

QUIC_EXPORT_PRIVATE bool operator==(const QuicHttpAltSvcFields& a,
                                    const QuicHttpAltSvcFields& b);
QUIC_EXPORT_PRIVATE inline bool operator!=(const QuicHttpAltSvcFields& a,
                                           const QuicHttpAltSvcFields& b) {
  return !(a == b);
}
QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                             const QuicHttpAltSvcFields& v);

}  // namespace net

#endif  // NET_QUIC_HTTP_QUIC_HTTP_STRUCTURES_H_
