// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/quic_http_structures.h"

#include <cstring>  // For std::memcmp

#include "net/quic/platform/api/quic_str_cat.h"
#include "net/quic/platform/api/quic_string_utils.h"
#include "net/quic/platform/api/quic_text_utils.h"

namespace net {

// QuicHttpFrameHeader:

bool QuicHttpFrameHeader::IsProbableHttpResponse() const {
  return (payload_length == 0x485454 &&      // "HTT"
          static_cast<char>(type) == 'P' &&  // "P"
          flags == '/');                     // "/"
}

QuicString QuicHttpFrameHeader::ToString() const {
  return QuicStrCat("length=", payload_length,
                    ", type=", QuicHttpFrameTypeToString(type),
                    ", flags=", FlagsToString(), ", stream=", stream_id);
}

QuicString QuicHttpFrameHeader::FlagsToString() const {
  return QuicHttpFrameFlagsToString(type, flags);
}

bool operator==(const QuicHttpFrameHeader& a, const QuicHttpFrameHeader& b) {
  return a.payload_length == b.payload_length && a.stream_id == b.stream_id &&
         a.type == b.type && a.flags == b.flags;
}

std::ostream& operator<<(std::ostream& out, const QuicHttpFrameHeader& v) {
  return out << v.ToString();
}

// QuicHttpPriorityFields:

bool operator==(const QuicHttpPriorityFields& a,
                const QuicHttpPriorityFields& b) {
  return a.stream_dependency == b.stream_dependency && a.weight == b.weight;
}

QuicString QuicHttpPriorityFields::ToString() const {
  std::stringstream ss;
  ss << "E=" << (is_exclusive ? "true" : "false")
     << ", stream=" << stream_dependency
     << ", weight=" << static_cast<uint32_t>(weight);
  return ss.str();
}

std::ostream& operator<<(std::ostream& out, const QuicHttpPriorityFields& v) {
  return out << "E=" << (v.is_exclusive ? "true" : "false")
             << ", stream=" << v.stream_dependency
             << ", weight=" << static_cast<uint32_t>(v.weight);
}

// QuicHttpRstStreamFields:

bool operator==(const QuicHttpRstStreamFields& a,
                const QuicHttpRstStreamFields& b) {
  return a.error_code == b.error_code;
}

std::ostream& operator<<(std::ostream& out, const QuicHttpRstStreamFields& v) {
  return out << "error_code=" << v.error_code;
}

// QuicHttpSettingFields:

bool operator==(const QuicHttpSettingFields& a,
                const QuicHttpSettingFields& b) {
  return a.parameter == b.parameter && a.value == b.value;
}
std::ostream& operator<<(std::ostream& out, const QuicHttpSettingFields& v) {
  return out << "parameter=" << v.parameter << ", value=" << v.value;
}

// QuicHttpPushPromiseFields:

bool operator==(const QuicHttpPushPromiseFields& a,
                const QuicHttpPushPromiseFields& b) {
  return a.promised_stream_id == b.promised_stream_id;
}

std::ostream& operator<<(std::ostream& out,
                         const QuicHttpPushPromiseFields& v) {
  return out << "promised_stream_id=" << v.promised_stream_id;
}

// QuicHttpPingFields:

bool operator==(const QuicHttpPingFields& a, const QuicHttpPingFields& b) {
  static_assert((sizeof a.opaque_bytes) == QuicHttpPingFields::EncodedSize(),
                "Why not the same size?");
  return 0 ==
         std::memcmp(a.opaque_bytes, b.opaque_bytes, sizeof a.opaque_bytes);
}

std::ostream& operator<<(std::ostream& out, const QuicHttpPingFields& v) {
  return out << "opaque_bytes=0x"
             << QuicTextUtils::HexEncode(
                    reinterpret_cast<const char*>(v.opaque_bytes),
                    sizeof v.opaque_bytes);
}

// QuicHttpGoAwayFields:

bool operator==(const QuicHttpGoAwayFields& a, const QuicHttpGoAwayFields& b) {
  return a.last_stream_id == b.last_stream_id && a.error_code == b.error_code;
}
std::ostream& operator<<(std::ostream& out, const QuicHttpGoAwayFields& v) {
  return out << "last_stream_id=" << v.last_stream_id
             << ", error_code=" << v.error_code;
}

// QuicHttpWindowUpdateFields:

bool operator==(const QuicHttpWindowUpdateFields& a,
                const QuicHttpWindowUpdateFields& b) {
  return a.window_size_increment == b.window_size_increment;
}
std::ostream& operator<<(std::ostream& out,
                         const QuicHttpWindowUpdateFields& v) {
  return out << "window_size_increment=" << v.window_size_increment;
}

// QuicHttpAltSvcFields:

bool operator==(const QuicHttpAltSvcFields& a, const QuicHttpAltSvcFields& b) {
  return a.origin_length == b.origin_length;
}
std::ostream& operator<<(std::ostream& out, const QuicHttpAltSvcFields& v) {
  return out << "origin_length=" << v.origin_length;
}

}  // namespace net
