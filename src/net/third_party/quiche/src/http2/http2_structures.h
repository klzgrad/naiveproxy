// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HTTP2_STRUCTURES_H_
#define QUICHE_HTTP2_HTTP2_STRUCTURES_H_

// Defines structs for various fixed sized structures in HTTP/2.
//
// Those structs with multiple fields have constructors that take arguments in
// the same order as their encoding (which may be different from their order
// in the struct). For single field structs, use aggregate initialization if
// desired, e.g.:
//
//   Http2RstStreamFields var{Http2ErrorCode::ENHANCE_YOUR_CALM};
// or:
//   SomeFunc(Http2RstStreamFields{Http2ErrorCode::ENHANCE_YOUR_CALM});
//
// Each struct includes a static method EncodedSize which returns the number
// of bytes of the encoding.
//
// With the exception of Http2FrameHeader, all the types are named
// Http2<X>Fields, where X is the title-case form of the frame which always
// includes the fields; the "always" is to cover the case of the PRIORITY frame;
// its fields optionally appear in the HEADERS frame, but the struct is called
// Http2PriorityFields.

#include <stddef.h>

#include <cstdint>
#include <ostream>
#include <string>

#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_export.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

namespace http2 {

struct HTTP2_EXPORT_PRIVATE Http2FrameHeader {
  Http2FrameHeader() {}
  Http2FrameHeader(uint32_t payload_length,
                   Http2FrameType type,
                   uint8_t flags,
                   uint32_t stream_id)
      : payload_length(payload_length),
        stream_id(stream_id),
        type(type),
        flags(static_cast<Http2FrameFlag>(flags)) {
    DCHECK_LT(payload_length, static_cast<uint32_t>(1 << 24))
        << "Payload Length is only a 24 bit field\n"
        << ToString();
  }

  static constexpr size_t EncodedSize() { return 9; }

  // Keep the current value of those flags that are in
  // valid_flags, and clear all the others.
  void RetainFlags(uint8_t valid_flags) {
    flags = static_cast<Http2FrameFlag>(flags & valid_flags);
  }

  // Returns true if any of the flags in flag_mask are set,
  // otherwise false.
  bool HasAnyFlags(uint8_t flag_mask) const { return 0 != (flags & flag_mask); }

  // Is the END_STREAM flag set?
  bool IsEndStream() const {
    DCHECK(type == Http2FrameType::DATA || type == Http2FrameType::HEADERS)
        << ToString();
    return (flags & Http2FrameFlag::END_STREAM) != 0;
  }

  // Is the ACK flag set?
  bool IsAck() const {
    DCHECK(type == Http2FrameType::SETTINGS || type == Http2FrameType::PING)
        << ToString();
    return (flags & Http2FrameFlag::ACK) != 0;
  }

  // Is the END_HEADERS flag set?
  bool IsEndHeaders() const {
    DCHECK(type == Http2FrameType::HEADERS ||
           type == Http2FrameType::PUSH_PROMISE ||
           type == Http2FrameType::CONTINUATION)
        << ToString();
    return (flags & Http2FrameFlag::END_HEADERS) != 0;
  }

  // Is the PADDED flag set?
  bool IsPadded() const {
    DCHECK(type == Http2FrameType::DATA || type == Http2FrameType::HEADERS ||
           type == Http2FrameType::PUSH_PROMISE)
        << ToString();
    return (flags & Http2FrameFlag::PADDED) != 0;
  }

  // Is the PRIORITY flag set?
  bool HasPriority() const {
    DCHECK_EQ(type, Http2FrameType::HEADERS) << ToString();
    return (flags & Http2FrameFlag::PRIORITY) != 0;
  }

  // Does the encoding of this header start with "HTTP/", indicating that it
  // might be from a non-HTTP/2 server.
  bool IsProbableHttpResponse() const;

  // Produce strings useful for debugging/logging messages.
  std::string ToString() const;
  std::string FlagsToString() const;

  // 24 bit length of the payload after the header, including any padding.
  // First field in encoding.
  uint32_t payload_length;  // 24 bits

  // 31 bit stream id, with high bit (32nd bit) reserved (must be zero),
  // and is cleared during decoding.
  // Fourth field in encoding.
  uint32_t stream_id;

  // Type of the frame.
  // Second field in encoding.
  Http2FrameType type;

  // Flag bits, with interpretations that depend upon the frame type.
  // Flag bits not used by the frame type are cleared.
  // Third field in encoding.
  Http2FrameFlag flags;
};

HTTP2_EXPORT_PRIVATE bool operator==(const Http2FrameHeader& a,
                                     const Http2FrameHeader& b);
HTTP2_EXPORT_PRIVATE inline bool operator!=(const Http2FrameHeader& a,
                                            const Http2FrameHeader& b) {
  return !(a == b);
}
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const Http2FrameHeader& v);

// Http2PriorityFields:

struct HTTP2_EXPORT_PRIVATE Http2PriorityFields {
  Http2PriorityFields() {}
  Http2PriorityFields(uint32_t stream_dependency,
                      uint32_t weight,
                      bool is_exclusive)
      : stream_dependency(stream_dependency),
        weight(weight),
        is_exclusive(is_exclusive) {
    // Can't have the high-bit set in the stream id because we need to use
    // that for the EXCLUSIVE flag bit.
    DCHECK_EQ(stream_dependency, stream_dependency & StreamIdMask())
        << "Stream Dependency is only a 31-bit field.\n"
        << ToString();
    DCHECK_LE(1u, weight) << "Weight is too small.";
    DCHECK_LE(weight, 256u) << "Weight is too large.";
  }
  static constexpr size_t EncodedSize() { return 5; }

  // Produce strings useful for debugging/logging messages.
  std::string ToString() const;

  // A 31-bit stream identifier for the stream that this stream depends on.
  uint32_t stream_dependency;

  // Weight (1 to 256) is encoded as a byte in the range 0 to 255, so we
  // add one when decoding, and store it in a field larger than a byte.
  uint32_t weight;

  // A single-bit flag indicating that the stream dependency is exclusive;
  // extracted from high bit of stream dependency field during decoding.
  bool is_exclusive;
};

HTTP2_EXPORT_PRIVATE bool operator==(const Http2PriorityFields& a,
                                     const Http2PriorityFields& b);
HTTP2_EXPORT_PRIVATE inline bool operator!=(const Http2PriorityFields& a,
                                            const Http2PriorityFields& b) {
  return !(a == b);
}
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const Http2PriorityFields& v);

// Http2RstStreamFields:

struct Http2RstStreamFields {
  static constexpr size_t EncodedSize() { return 4; }
  bool IsSupportedErrorCode() const {
    return IsSupportedHttp2ErrorCode(error_code);
  }

  Http2ErrorCode error_code;
};

HTTP2_EXPORT_PRIVATE bool operator==(const Http2RstStreamFields& a,
                                     const Http2RstStreamFields& b);
HTTP2_EXPORT_PRIVATE inline bool operator!=(const Http2RstStreamFields& a,
                                            const Http2RstStreamFields& b) {
  return !(a == b);
}
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const Http2RstStreamFields& v);

// Http2SettingFields:

struct Http2SettingFields {
  Http2SettingFields() {}
  Http2SettingFields(Http2SettingsParameter parameter, uint32_t value)
      : parameter(parameter), value(value) {}
  static constexpr size_t EncodedSize() { return 6; }
  bool IsSupportedParameter() const {
    return IsSupportedHttp2SettingsParameter(parameter);
  }

  Http2SettingsParameter parameter;
  uint32_t value;
};

HTTP2_EXPORT_PRIVATE bool operator==(const Http2SettingFields& a,
                                     const Http2SettingFields& b);
HTTP2_EXPORT_PRIVATE inline bool operator!=(const Http2SettingFields& a,
                                            const Http2SettingFields& b) {
  return !(a == b);
}
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const Http2SettingFields& v);

// Http2PushPromiseFields:

struct Http2PushPromiseFields {
  static constexpr size_t EncodedSize() { return 4; }

  uint32_t promised_stream_id;
};

HTTP2_EXPORT_PRIVATE bool operator==(const Http2PushPromiseFields& a,
                                     const Http2PushPromiseFields& b);
HTTP2_EXPORT_PRIVATE inline bool operator!=(const Http2PushPromiseFields& a,
                                            const Http2PushPromiseFields& b) {
  return !(a == b);
}
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const Http2PushPromiseFields& v);

// Http2PingFields:

struct Http2PingFields {
  static constexpr size_t EncodedSize() { return 8; }

  uint8_t opaque_bytes[8];
};

HTTP2_EXPORT_PRIVATE bool operator==(const Http2PingFields& a,
                                     const Http2PingFields& b);
HTTP2_EXPORT_PRIVATE inline bool operator!=(const Http2PingFields& a,
                                            const Http2PingFields& b) {
  return !(a == b);
}
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const Http2PingFields& v);

// Http2GoAwayFields:

struct Http2GoAwayFields {
  Http2GoAwayFields() {}
  Http2GoAwayFields(uint32_t last_stream_id, Http2ErrorCode error_code)
      : last_stream_id(last_stream_id), error_code(error_code) {}
  static constexpr size_t EncodedSize() { return 8; }
  bool IsSupportedErrorCode() const {
    return IsSupportedHttp2ErrorCode(error_code);
  }

  uint32_t last_stream_id;
  Http2ErrorCode error_code;
};

HTTP2_EXPORT_PRIVATE bool operator==(const Http2GoAwayFields& a,
                                     const Http2GoAwayFields& b);
HTTP2_EXPORT_PRIVATE inline bool operator!=(const Http2GoAwayFields& a,
                                            const Http2GoAwayFields& b) {
  return !(a == b);
}
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const Http2GoAwayFields& v);

// Http2WindowUpdateFields:

struct Http2WindowUpdateFields {
  static constexpr size_t EncodedSize() { return 4; }

  // 31-bit, unsigned increase in the window size (only positive values are
  // allowed). The high-bit is reserved for the future.
  uint32_t window_size_increment;
};

HTTP2_EXPORT_PRIVATE bool operator==(const Http2WindowUpdateFields& a,
                                     const Http2WindowUpdateFields& b);
HTTP2_EXPORT_PRIVATE inline bool operator!=(const Http2WindowUpdateFields& a,
                                            const Http2WindowUpdateFields& b) {
  return !(a == b);
}
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const Http2WindowUpdateFields& v);

// Http2AltSvcFields:

struct Http2AltSvcFields {
  static constexpr size_t EncodedSize() { return 2; }

  // This is the one fixed size portion of the ALTSVC payload.
  uint16_t origin_length;
};

HTTP2_EXPORT_PRIVATE bool operator==(const Http2AltSvcFields& a,
                                     const Http2AltSvcFields& b);
HTTP2_EXPORT_PRIVATE inline bool operator!=(const Http2AltSvcFields& a,
                                            const Http2AltSvcFields& b) {
  return !(a == b);
}
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const Http2AltSvcFields& v);

}  // namespace http2

#endif  // QUICHE_HTTP2_HTTP2_STRUCTURES_H_
