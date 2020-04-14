// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HTTP2_CONSTANTS_H_
#define QUICHE_HTTP2_HTTP2_CONSTANTS_H_

// Constants from the HTTP/2 spec, RFC 7540, and associated helper functions.

#include <cstdint>
#include <iosfwd>
#include <ostream>
#include <string>

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {

// TODO(jamessynge): create http2_simple_types for types similar to
// SpdyStreamId, but not for structures like Http2FrameHeader. Then will be
// able to move these stream id functions there.
constexpr uint32_t UInt31Mask() {
  return 0x7fffffff;
}
constexpr uint32_t StreamIdMask() {
  return UInt31Mask();
}

// The value used to identify types of frames. Upper case to match the RFC.
// The comments indicate which flags are valid for that frame type.
// ALTSVC is defined in http://httpwg.org/http-extensions/alt-svc.html
// (not yet final standard as of March 2016, but close).
enum class Http2FrameType : uint8_t {
  DATA = 0,           // END_STREAM | PADDED
  HEADERS = 1,        // END_STREAM | END_HEADERS | PADDED | PRIORITY
  PRIORITY = 2,       //
  RST_STREAM = 3,     //
  SETTINGS = 4,       // ACK
  PUSH_PROMISE = 5,   // END_HEADERS | PADDED
  PING = 6,           // ACK
  GOAWAY = 7,         //
  WINDOW_UPDATE = 8,  //
  CONTINUATION = 9,   // END_HEADERS
  ALTSVC = 10,        //
};

// Is the frame type known/supported?
inline bool IsSupportedHttp2FrameType(uint32_t v) {
  return v <= static_cast<uint32_t>(Http2FrameType::ALTSVC);
}
inline bool IsSupportedHttp2FrameType(Http2FrameType v) {
  return IsSupportedHttp2FrameType(static_cast<uint32_t>(v));
}

// The return type is 'std::string' so that they can generate a unique string
// for each unsupported value. Since these are just used for debugging/error
// messages, that isn't a cost to we need to worry about. The same applies to
// the functions later in this file.
QUICHE_EXPORT_PRIVATE std::string Http2FrameTypeToString(Http2FrameType v);
QUICHE_EXPORT_PRIVATE std::string Http2FrameTypeToString(uint8_t v);
QUICHE_EXPORT_PRIVATE inline std::ostream& operator<<(std::ostream& out,
                                                      Http2FrameType v) {
  return out << Http2FrameTypeToString(v);
}

// Flags that appear in supported frame types. These are treated as bit masks.
// The comments indicate for which frame types the flag is valid.
enum Http2FrameFlag {
  END_STREAM = 0x01,   // DATA, HEADERS
  ACK = 0x01,          // SETTINGS, PING
  END_HEADERS = 0x04,  // HEADERS, PUSH_PROMISE, CONTINUATION
  PADDED = 0x08,       // DATA, HEADERS, PUSH_PROMISE
  PRIORITY = 0x20,     // HEADERS
};

// Formats zero or more flags for the specified type of frame. Returns an
// empty string if flags==0.
QUICHE_EXPORT_PRIVATE std::string Http2FrameFlagsToString(Http2FrameType type,
                                                          uint8_t flags);
QUICHE_EXPORT_PRIVATE std::string Http2FrameFlagsToString(uint8_t type,
                                                          uint8_t flags);

// Error codes for GOAWAY and RST_STREAM frames.
enum class Http2ErrorCode : uint32_t {
  // The associated condition is not a result of an error. For example, a GOAWAY
  // might include this code to indicate graceful shutdown of a connection.
  HTTP2_NO_ERROR = 0x0,

  // The endpoint detected an unspecific protocol error. This error is for use
  // when a more specific error code is not available.
  PROTOCOL_ERROR = 0x1,

  // The endpoint encountered an unexpected internal error.
  INTERNAL_ERROR = 0x2,

  // The endpoint detected that its peer violated the flow-control protocol.
  FLOW_CONTROL_ERROR = 0x3,

  // The endpoint sent a SETTINGS frame but did not receive a response in a
  // timely manner. See Section 6.5.3 ("Settings Synchronization").
  SETTINGS_TIMEOUT = 0x4,

  // The endpoint received a frame after a stream was half-closed.
  STREAM_CLOSED = 0x5,

  // The endpoint received a frame with an invalid size.
  FRAME_SIZE_ERROR = 0x6,

  // The endpoint refused the stream prior to performing any application
  // processing (see Section 8.1.4 for details).
  REFUSED_STREAM = 0x7,

  // Used by the endpoint to indicate that the stream is no longer needed.
  CANCEL = 0x8,

  // The endpoint is unable to maintain the header compression context
  // for the connection.
  COMPRESSION_ERROR = 0x9,

  // The connection established in response to a CONNECT request (Section 8.3)
  // was reset or abnormally closed.
  CONNECT_ERROR = 0xa,

  // The endpoint detected that its peer is exhibiting a behavior that might
  // be generating excessive load.
  ENHANCE_YOUR_CALM = 0xb,

  // The underlying transport has properties that do not meet minimum
  // security requirements (see Section 9.2).
  INADEQUATE_SECURITY = 0xc,

  // The endpoint requires that HTTP/1.1 be used instead of HTTP/2.
  HTTP_1_1_REQUIRED = 0xd,
};

// Is the error code supported? (So far that means it is in RFC 7540.)
inline bool IsSupportedHttp2ErrorCode(uint32_t v) {
  return v <= static_cast<uint32_t>(Http2ErrorCode::HTTP_1_1_REQUIRED);
}
inline bool IsSupportedHttp2ErrorCode(Http2ErrorCode v) {
  return IsSupportedHttp2ErrorCode(static_cast<uint32_t>(v));
}

// Format the specified error code.
QUICHE_EXPORT_PRIVATE std::string Http2ErrorCodeToString(uint32_t v);
QUICHE_EXPORT_PRIVATE std::string Http2ErrorCodeToString(Http2ErrorCode v);
QUICHE_EXPORT_PRIVATE inline std::ostream& operator<<(std::ostream& out,
                                                      Http2ErrorCode v) {
  return out << Http2ErrorCodeToString(v);
}

// Supported parameters in SETTINGS frames; so far just those in RFC 7540.
enum class Http2SettingsParameter : uint16_t {
  // Allows the sender to inform the remote endpoint of the maximum size of the
  // header compression table used to decode header blocks, in octets. The
  // encoder can select any size equal to or less than this value by using
  // signaling specific to the header compression format inside a header block
  // (see [COMPRESSION]). The initial value is 4,096 octets.
  HEADER_TABLE_SIZE = 0x1,

  // This setting can be used to disable server push (Section 8.2). An endpoint
  // MUST NOT send a PUSH_PROMISE frame if it receives this parameter set to a
  // value of 0. An endpoint that has both set this parameter to 0 and had it
  // acknowledged MUST treat the receipt of a PUSH_PROMISE frame as a connection
  // error (Section 5.4.1) of type PROTOCOL_ERROR.
  //
  // The initial value is 1, which indicates that server push is permitted. Any
  // value other than 0 or 1 MUST be treated as a connection error (Section
  // 5.4.1) of type PROTOCOL_ERROR.
  ENABLE_PUSH = 0x2,

  // Indicates the maximum number of concurrent streams that the sender will
  // allow. This limit is directional: it applies to the number of streams that
  // the sender permits the receiver to create. Initially, there is no limit to
  // this value. It is recommended that this value be no smaller than 100, so as
  // to not unnecessarily limit parallelism.
  //
  // A value of 0 for MAX_CONCURRENT_STREAMS SHOULD NOT be treated as
  // special by endpoints. A zero value does prevent the creation of new
  // streams; however, this can also happen for any limit that is exhausted with
  // active streams. Servers SHOULD only set a zero value for short durations;
  // if a server does not wish to accept requests, closing the connection is
  // more appropriate.
  MAX_CONCURRENT_STREAMS = 0x3,

  // Indicates the sender's initial window size (in octets) for stream-level
  // flow control. The initial value is 2^16-1 (65,535) octets.
  //
  // This setting affects the window size of all streams (see Section 6.9.2).
  //
  // Values above the maximum flow-control window size of 2^31-1 MUST be treated
  // as a connection error (Section 5.4.1) of type FLOW_CONTROL_ERROR.
  INITIAL_WINDOW_SIZE = 0x4,

  // Indicates the size of the largest frame payload that the sender is willing
  // to receive, in octets.
  //
  // The initial value is 2^14 (16,384) octets. The value advertised by an
  // endpoint MUST be between this initial value and the maximum allowed frame
  // size (2^24-1 or 16,777,215 octets), inclusive. Values outside this range
  // MUST be treated as a connection error (Section 5.4.1) of type
  // PROTOCOL_ERROR.
  MAX_FRAME_SIZE = 0x5,

  // This advisory setting informs a peer of the maximum size of header list
  // that the sender is prepared to accept, in octets. The value is based on the
  // uncompressed size of header fields, including the length of the name and
  // value in octets plus an overhead of 32 octets for each header field.
  //
  // For any given request, a lower limit than what is advertised MAY be
  // enforced. The initial value of this setting is unlimited.
  MAX_HEADER_LIST_SIZE = 0x6,
};

// Is the settings parameter supported (so far that means it is in RFC 7540)?
inline bool IsSupportedHttp2SettingsParameter(uint32_t v) {
  return 0 < v && v <= static_cast<uint32_t>(
                           Http2SettingsParameter::MAX_HEADER_LIST_SIZE);
}
inline bool IsSupportedHttp2SettingsParameter(Http2SettingsParameter v) {
  return IsSupportedHttp2SettingsParameter(static_cast<uint32_t>(v));
}

// Format the specified settings parameter.
QUICHE_EXPORT_PRIVATE std::string Http2SettingsParameterToString(uint32_t v);
QUICHE_EXPORT_PRIVATE std::string Http2SettingsParameterToString(
    Http2SettingsParameter v);
inline std::ostream& operator<<(std::ostream& out, Http2SettingsParameter v) {
  return out << Http2SettingsParameterToString(v);
}

// Information about the initial, minimum and maximum value of settings (not
// applicable to all settings parameters).
class Http2SettingsInfo {
 public:
  // Default value for HEADER_TABLE_SIZE.
  static constexpr uint32_t DefaultHeaderTableSize() { return 4096; }

  // Default value for ENABLE_PUSH.
  static constexpr bool DefaultEnablePush() { return true; }

  // Default value for INITIAL_WINDOW_SIZE.
  static constexpr uint32_t DefaultInitialWindowSize() { return 65535; }

  // Maximum value for INITIAL_WINDOW_SIZE, and for the connection flow control
  // window, and for each stream flow control window.
  static constexpr uint32_t MaximumWindowSize() { return UInt31Mask(); }

  // Default value for MAX_FRAME_SIZE.
  static constexpr uint32_t DefaultMaxFrameSize() { return 16384; }

  // Minimum value for MAX_FRAME_SIZE.
  static constexpr uint32_t MinimumMaxFrameSize() { return 16384; }

  // Maximum value for MAX_FRAME_SIZE.
  static constexpr uint32_t MaximumMaxFrameSize() { return (1 << 24) - 1; }
};

}  // namespace http2

#endif  // QUICHE_HTTP2_HTTP2_CONSTANTS_H_
